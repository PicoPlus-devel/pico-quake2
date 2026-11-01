// fcache.c -- flash asset cache (see fcache.h).
//
// Region layout: two directory slots of 128 KB (alternating, the newer valid one
// wins, so an interrupted commit loses only the changes since the previous one;
// during a level's load the directory is written every FC_COMMIT_BYTES), then the
// data area. Every asset occupies a contiguous run of 256-byte pages so it can be
// used in place; small assets share 4 KB erase sectors. NOR flash can program
// still-erased pages without an erase, so a sector is only erased when a new asset
// needs pages in it that were written before, and only if nothing live remains in it.
// The directory is mirrored in RAM; the page maps are rebuilt from it at boot.

#include "qcommon.h"
#include "fcache.h"

#define FC_MAGIC		(('C'<<24)|('F'<<16)|('2'<<8)|'Q')
#define FC_VERSION		1
#define FC_MAX_ENTRIES	2047		// header + entries fill one 128 KB slot
#define FC_NAMELEN		44
#define FC_SLOT_BYTES	(128 * 1024)
#define FC_DATA_START	(2 * FC_SLOT_BYTES)
#define FC_MAX_PAGES	(16 * 1024 * 1024 / FC_PAGE)
#define FC_SECTOR_PAGES	(FC_SECTOR / FC_PAGE)
#define FC_COMMIT_BYTES	(1024 * 1024)	// stored data after which the directory is written
#define FC_COMMIT_CHUNK	4096			// directory bytes built and programmed at a time
#define FC_NOTICE_BYTES	(512 * 1024)	// stored data of a level after which its load is called slow

typedef struct
{
	unsigned	magic, version, generation, num_entries;
	unsigned	data_id;		// identity of the paks the entries came from
	unsigned	seq;			// level sequence at the last commit
	unsigned	dir_crc;		// CRC-32 of the entries
	unsigned	pad[9];
} fc_header_t;

typedef struct
{
	char		name[FC_NAMELEN];
	unsigned	offset;			// from the region base, sector aligned; 0 = unused slot
	unsigned	length;
	unsigned	crc;			// CRC-32 of the data
	unsigned short	kind;
	unsigned short	flags;		// on flash: 0
	unsigned	last_seq;		// level sequence that last used the entry
} fc_entry_t;

// RAM-only entry flags
#define FCF_VERIFIED	1		// data CRC checked this boot
#define FCF_PINNED		2

static const fc_flash_t	*flash;
static qboolean		loaded;
static fc_header_t	hdr;
static fc_entry_t	dir[FC_MAX_ENTRIES];
static int			numentries;
static int			active_slot;
static qboolean		dirty;
static unsigned		cur_seq;
static char			cur_map[MAX_QPATH];
static unsigned		data_pages;
static byte			page_used[FC_MAX_PAGES / 8];	// holds live data
static byte			page_blank[FC_MAX_PAGES / 8];	// erased, can be programmed as is
static byte			sector_blocked[FC_MAX_PAGES / FC_SECTOR_PAGES];	// FC_EvictWindow scratch
static byte			sector_cost[FC_MAX_PAGES / FC_SECTOR_PAGES];
static fc_stats_t	stats;
static qboolean		overflow;		// an asset of this level did not fit
static unsigned		uncommitted;	// bytes stored since the directory was last written
static unsigned		level_stored;	// bytes stored since the map changed
static qboolean		level_notice;	// the first-load notice was printed for this map

/*
==============================================================================

CRC-32 (IEEE, reflected)

==============================================================================
*/

static unsigned	crc_table[256];

static void FC_CrcInit (void)
{
	int		i, k;

	for (i = 0; i < 256; i++)
	{
		unsigned	v = i;
		for (k = 0; k < 8; k++)
			v = (v & 1) ? 0xedb88320 ^ (v >> 1) : v >> 1;
		crc_table[i] = v;
	}
}

// Running CRC: start with 0xffffffff, invert the result
static unsigned FC_CrcUpdate (unsigned c, const void *data, int len)
{
	const byte	*p = data;

	while (len--)
		c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return c;
}

static unsigned FC_Crc (const void *data, int len)
{
	return ~FC_CrcUpdate (0xffffffff, data, len);
}

/*
==============================================================================

PAGE MAPS

==============================================================================
*/

#define BIT_GET(map, i)		(((map)[(i) >> 3] >> ((i) & 7)) & 1)
#define BIT_SET(map, i)		((map)[(i) >> 3] |= 1 << ((i) & 7))
#define BIT_CLR(map, i)		((map)[(i) >> 3] &= ~(1 << ((i) & 7)))

static unsigned FC_PageOf (unsigned offset)
{
	return (offset - FC_DATA_START) / FC_PAGE;
}

static void FC_MarkPages (unsigned offset, unsigned length, qboolean used)
{
	unsigned	p = FC_PageOf (offset);
	unsigned	n = (length + FC_PAGE - 1) / FC_PAGE;

	for ( ; n--; p++)
	{
		if (used)
		{
			BIT_SET (page_used, p);
			BIT_CLR (page_blank, p);
		}
		else
			BIT_CLR (page_used, p);
	}
}

static qboolean FC_SectorHasLive (unsigned page)
{
	unsigned	first = page - page % FC_SECTOR_PAGES;
	unsigned	i;

	for (i = 0; i < FC_SECTOR_PAGES; i++)
		if (BIT_GET (page_used, first + i))
			return true;
	return false;
}

// A page can take new data if it is free and either still erased, or in a sector
// with nothing live in it (which can then be erased).
static qboolean FC_PageAvailable (unsigned p, qboolean blank_only)
{
	if (BIT_GET (page_used, p))
		return false;
	if (BIT_GET (page_blank, p))
		return true;
	return !blank_only && !FC_SectorHasLive (p);
}

// Smallest run of at least n available pages; returns the first page or -1.
static int FC_BestFit (unsigned n, qboolean blank_only)
{
	unsigned	p, run_start = 0, run = 0;
	int			best = -1;
	unsigned	best_len = 0xffffffff;

	for (p = 0; p <= data_pages; p++)
	{
		if (p < data_pages && FC_PageAvailable (p, blank_only))
		{
			if (!run)
				run_start = p;
			run++;
			continue;
		}
		if (run >= n && run < best_len)
		{
			best = run_start;
			best_len = run;
		}
		run = 0;
	}
	return best;
}

// Erases the sectors under pages [first, first+n) that hold written pages.
static void FC_PrepareRun (unsigned first, unsigned n)
{
	unsigned	s, end_s, p, erase_from = 0, erase_count = 0;

	s = first / FC_SECTOR_PAGES;
	end_s = (first + n - 1) / FC_SECTOR_PAGES;
	for ( ; s <= end_s + 1; s++)
	{
		qboolean	need = false;

		if (s <= end_s)
			for (p = s * FC_SECTOR_PAGES; p < (s + 1) * FC_SECTOR_PAGES; p++)
				if (p >= first && p < first + n && !BIT_GET (page_blank, p))
					need = true;
		if (need)
		{
			if (!erase_count)
				erase_from = s;
			erase_count++;
			continue;
		}
		if (erase_count)
		{
			flash->erase (FC_DATA_START + erase_from * FC_SECTOR, erase_count * FC_SECTOR);
			stats.bytes_erased += erase_count * FC_SECTOR;
			for (p = erase_from * FC_SECTOR_PAGES; p < (erase_from + erase_count) * FC_SECTOR_PAGES; p++)
				BIT_SET (page_blank, p);
			erase_count = 0;
		}
	}
}

static void FC_RebuildPageMaps (void)
{
	unsigned	p, i;
	const byte	*d;

	memset (page_used, 0, sizeof(page_used));
	stats.used_bytes = 0;
	for (i = 0; i < (unsigned)numentries; i++)
	{
		if (!dir[i].offset)
			continue;
		FC_MarkPages (dir[i].offset, dir[i].length, true);
		stats.used_bytes += dir[i].length;
	}
	// which free pages are still erased (one read of the free space per boot)
	memset (page_blank, 0, sizeof(page_blank));
	for (p = 0; p < data_pages; p++)
	{
		if (BIT_GET (page_used, p))
			continue;
		d = flash->base + FC_DATA_START + p * FC_PAGE;
		for (i = 0; i < FC_PAGE && d[i] == 0xff; i++)
			;
		if (i == FC_PAGE)
			BIT_SET (page_blank, p);
	}
}

/*
==============================================================================

DIRECTORY

==============================================================================
*/

static qboolean FC_SlotValid (int slot, fc_header_t *h)
{
	const byte	*p = flash->base + slot * FC_SLOT_BYTES;

	memcpy (h, p, sizeof(*h));
	if (h->magic != FC_MAGIC || h->version != FC_VERSION || h->num_entries > FC_MAX_ENTRIES)
		return false;
	return FC_Crc (p + sizeof(*h), h->num_entries * sizeof(fc_entry_t)) == h->dir_crc;
}

static void FC_LoadDirectory (void)
{
	fc_header_t	h0, h1;
	qboolean	v0, v1;
	unsigned	data_id;
	int			i;

	loaded = true;
	FC_CrcInit ();
	data_pages = (flash->size - FC_DATA_START) / FC_PAGE;
	if (data_pages > FC_MAX_PAGES)
		data_pages = FC_MAX_PAGES;
	stats.capacity = data_pages * FC_PAGE;

	v0 = FC_SlotValid (0, &h0);
	v1 = FC_SlotValid (1, &h1);
	numentries = 0;
	memset (&hdr, 0, sizeof(hdr));
	active_slot = 1;		// so the first commit goes to slot 0

	data_id = FS_DataIdentity ();
	if (v0 || v1)
	{
		active_slot = (v1 && (!v0 || h1.generation > h0.generation)) ? 1 : 0;
		hdr = active_slot ? h1 : h0;
		if (hdr.data_id != data_id)
		{
			Com_Printf ("fcache: game data changed, discarding cached assets\n");
			hdr.num_entries = 0;
			dirty = true;
		}
		numentries = hdr.num_entries;
		memcpy (dir, flash->base + active_slot * FC_SLOT_BYTES + sizeof(hdr), numentries * sizeof(fc_entry_t));
		for (i = 0; i < numentries; i++)
		{
			dir[i].flags = 0;
			if (dir[i].offset && (dir[i].offset < FC_DATA_START
				|| dir[i].offset + dir[i].length > FC_DATA_START + data_pages * FC_PAGE))
				dir[i].offset = 0;		// does not fit this region (size changed)
		}
	}
	hdr.data_id = data_id;
	cur_seq = hdr.seq;
	FC_RebuildPageMaps ();
	Com_Printf ("fcache: %i KB region, %i cached assets, %i KB used\n",
		flash->size / 1024, numentries, stats.used_bytes / 1024);
}

static qboolean FC_Ready (void)
{
	if (!flash)
		return false;
	if (!loaded)
		FC_LoadDirectory ();
	return true;
}

static void FC_RemoveEntry (int i)
{
	if (!dir[i].offset)
		return;
	FC_MarkPages (dir[i].offset, dir[i].length, false);
	stats.used_bytes -= dir[i].length;
	dir[i].offset = 0;
	dirty = true;
}

static int FC_Lookup (const char *name, int kind)
{
	int		i;

	for (i = 0; i < numentries; i++)
		if (dir[i].offset && dir[i].kind == kind && !strcmp (dir[i].name, name))
			return i;
	return -1;
}

/*
==============================================================================

PUBLIC

==============================================================================
*/

void FC_SetFlash (const fc_flash_t *f)
{
	flash = f;
	loaded = false;
}

const void *FC_Find (const char *name, int kind, int *length)
{
	int		i;

	if (!FC_Ready ())
		return NULL;
	i = FC_Lookup (name, kind);
	if (i < 0)
	{
		stats.misses++;
		return NULL;
	}
	if (!(dir[i].flags & FCF_VERIFIED))
	{
		// the region may have been overwritten by another application
		if (FC_Crc (flash->base + dir[i].offset, dir[i].length) != dir[i].crc)
		{
			Com_DPrintf ("fcache: %s is damaged, reloading\n", name);
			FC_RemoveEntry (i);
			stats.misses++;
			return NULL;
		}
		dir[i].flags |= FCF_VERIFIED;
	}
	if (dir[i].last_seq != cur_seq)
	{
		dir[i].last_seq = cur_seq;
		dirty = true;
	}
	stats.hits++;
	if (length)
		*length = dir[i].length;
	return flash->base + dir[i].offset;
}

// Evicts the least recently used entry that no live object can reference.
static qboolean FC_EvictOne (void)
{
	int		i, best = -1;

	for (i = 0; i < numentries; i++)
	{
		if (!dir[i].offset || dir[i].last_seq == cur_seq || (dir[i].flags & FCF_PINNED))
			continue;
		if (best < 0 || dir[i].last_seq < dir[best].last_seq
			|| (dir[i].last_seq == dir[best].last_seq && dir[i].length > dir[best].length))
			best = i;
	}
	if (best < 0)
		return false;
	FC_RemoveEntry (best);
	stats.evictions++;
	return true;
}

static qboolean FC_EvictWindow (unsigned pages);

// Finds room for len bytes and prepares it; returns the directory slot or -1.
static int FC_Allocate (const char *name, int kind, int len, unsigned *offset_out)
{
	unsigned	pages;
	int			first, i, slot;

	if (!FC_Ready () || len <= 0 || strlen (name) >= FC_NAMELEN)
		return -1;

	i = FC_Lookup (name, kind);
	if (i >= 0)
		FC_RemoveEntry (i);

	// a directory slot
	for (slot = 0; slot < numentries; slot++)
		if (!dir[slot].offset)
			break;
	while (slot == FC_MAX_ENTRIES)
	{
		if (!FC_EvictOne ())
		{
			stats.store_failures++;
			return -1;
		}
		for (slot = 0; slot < numentries; slot++)
			if (!dir[slot].offset)
				break;
	}

	// a contiguous run of pages: preferably still erased, else in sectors that can be
	// erased, else evict
	pages = (len + FC_PAGE - 1) / FC_PAGE;
	for (;;)
	{
		first = FC_BestFit (pages, true);
		if (first < 0)
			first = FC_BestFit (pages, false);
		if (first >= 0)
			break;
		if (!FC_EvictWindow (pages))
		{
			Com_DPrintf ("fcache: no room for %s (%i KB)\n", name, len / 1024);
			stats.store_failures++;
			overflow = true;
			return -1;
		}
	}

	FC_PrepareRun (first, pages);
	for (i = first; i < first + (int)pages; i++)
		BIT_CLR (page_blank, i);		// about to be written, whether or not it verifies
	stats.bytes_programmed += pages * FC_PAGE;
	*offset_out = FC_DATA_START + first * FC_PAGE;
	return slot;
}

// Records a written entry if the flash holds what was meant to be written.
static const void *FC_Finish (int slot, const char *name, int kind, unsigned offset, int len, unsigned crc)
{
	memset (&dir[slot], 0, sizeof(dir[slot]));
	if (FC_Crc (flash->base + offset, len) != crc)
	{
		Com_Printf ("fcache: write of %s did not verify\n", name);
		stats.store_failures++;
		return NULL;
	}
	strcpy (dir[slot].name, name);
	dir[slot].offset = offset;
	dir[slot].length = len;
	dir[slot].kind = kind;
	dir[slot].last_seq = cur_seq;
	dir[slot].crc = crc;
	dir[slot].flags = FCF_VERIFIED;
	if (slot == numentries)
		numentries++;
	FC_MarkPages (offset, len, true);
	stats.used_bytes += len;
	stats.stores++;
	dirty = true;

	// the first load of a level copies several MB and takes minutes: say so, and
	// write the directory every FC_COMMIT_BYTES, so that a reset in the middle of
	// it keeps what was already copied
	level_stored += len;
	if (!level_notice && level_stored >= FC_NOTICE_BYTES)
	{
		Com_Printf ("\nFirst load of this level: copying its\n"
			"data from the SD card to flash. This\n"
			"can take a few minutes; later loads\n"
			"are fast.\n\n");
		level_notice = true;
	}
	uncommitted += len;
	if (uncommitted >= FC_COMMIT_BYTES)
		FC_Commit ();
	return flash->base + offset;
}

// Programs len bytes at offset; a partial last page is padded with 0xff.
static void FC_Program (unsigned offset, const void *data, int len)
{
	byte		page[FC_PAGE];
	unsigned	full = len & ~(FC_PAGE - 1);

	if (full)
		flash->program (offset, data, full);
	if ((unsigned)len > full)
	{
		memset (page, 0xff, sizeof(page));
		memcpy (page, (const byte *)data + full, len - full);
		flash->program (offset + full, page, FC_PAGE);
	}
}

/*
================
FC_EvictWindow

When no run of free pages is long enough, evicts the entries in one sector-aligned
window of the needed size: the window holding the fewest pages of evictable data and
nothing in use this level. Evicting the globally oldest entries one by one would
leave holes scattered over the region, none of them large enough.
================
*/
static qboolean FC_Protected (int i)
{
	return dir[i].last_seq == cur_seq || (dir[i].flags & FCF_PINNED);
}

static qboolean FC_EvictWindow (unsigned pages)
{
	unsigned	nsect = data_pages / FC_SECTOR_PAGES;
	unsigned	need = (pages + FC_SECTOR_PAGES - 1) / FC_SECTOR_PAGES;
	unsigned	s, first, last, cost, best_cost = 0xffffffff, blocked = 0;
	int			i, best = -1;

	if (need > nsect)
		return false;
	memset (sector_blocked, 0, nsect);
	memset (sector_cost, 0, nsect);
	for (i = 0; i < numentries; i++)
	{
		if (!dir[i].offset)
			continue;
		first = FC_PageOf (dir[i].offset);
		last = first + (dir[i].length + FC_PAGE - 1) / FC_PAGE - 1;
		for (s = first / FC_SECTOR_PAGES; s <= last / FC_SECTOR_PAGES; s++)
		{
			if (FC_Protected (i))
				sector_blocked[s] = 1;
			else
				sector_cost[s]++;
		}
	}

	// sliding window over sectors
	cost = 0;
	for (s = 0; s < nsect; s++)
	{
		cost += sector_cost[s];
		blocked += sector_blocked[s];
		if (s >= need)
		{
			cost -= sector_cost[s - need];
			blocked -= sector_blocked[s - need];
		}
		if (s + 1 >= need && !blocked && cost < best_cost)
		{
			best_cost = cost;
			best = s + 1 - need;
		}
	}
	if (best < 0 || !best_cost)
		return false;		// nothing to evict would help

	for (i = 0; i < numentries; i++)
	{
		if (!dir[i].offset)
			continue;
		first = FC_PageOf (dir[i].offset) / FC_SECTOR_PAGES;
		last = (FC_PageOf (dir[i].offset) + (dir[i].length + FC_PAGE - 1) / FC_PAGE - 1) / FC_SECTOR_PAGES;
		if (last >= (unsigned)best && first < (unsigned)best + need)
		{
			FC_RemoveEntry (i);
			stats.evictions++;
		}
	}
	return true;
}

const void *FC_Store (const char *name, int kind, const void *data, int len)
{
	unsigned	offset;
	int			slot;

	slot = FC_Allocate (name, kind, len, &offset);
	if (slot < 0)
		return NULL;
	FC_Program (offset, data, len);
	return FC_Finish (slot, name, kind, offset, len, FC_Crc (data, len));
}

int FC_WriteBegin (fc_writer_t *w, const char *name, int kind, int len)
{
	memset (w, 0, sizeof(*w));
	w->slot = FC_Allocate (name, kind, len, &w->offset);
	if (w->slot < 0)
		return false;
	strncpy (w->name, name, sizeof(w->name) - 1);
	w->kind = kind;
	w->len = len;
	w->crc = 0xffffffff;
	return true;
}

void FC_Write (fc_writer_t *w, const void *data, int n)
{
	const byte	*src = data;
	int			i, take;

	if (w->slot < 0 || w->done + w->carry + n > w->len)
	{
		w->slot = -1;		// overrun: FC_WriteEnd fails
		return;
	}
	for (i = 0; i < n; i++)
		w->crc = crc_table[(w->crc ^ src[i]) & 0xff] ^ (w->crc >> 8);

	// fill the carried partial page, then program whole pages straight from data
	while (n > 0)
	{
		if (w->carry || n < FC_PAGE)
		{
			take = FC_PAGE - w->carry < n ? FC_PAGE - w->carry : n;
			memcpy (w->page + w->carry, src, take);
			w->carry += take;
			src += take;
			n -= take;
			if (w->carry == FC_PAGE)
			{
				flash->program (w->offset + w->done, w->page, FC_PAGE);
				w->done += FC_PAGE;
				w->carry = 0;
			}
			continue;
		}
		take = n & ~(FC_PAGE - 1);
		flash->program (w->offset + w->done, src, take);
		w->done += take;
		src += take;
		n -= take;
	}
}

const void *FC_WriteEnd (fc_writer_t *w)
{
	if (w->slot < 0)
		return NULL;
	if (w->carry)
	{
		memset (w->page + w->carry, 0xff, FC_PAGE - w->carry);
		flash->program (w->offset + w->done, w->page, FC_PAGE);
		w->done += w->carry;
		w->carry = 0;
	}
	if (w->done != w->len)
		return NULL;		// pages stay marked written; the entry is not recorded
	return FC_Finish (w->slot, w->name, w->kind, w->offset, w->len, ~w->crc);
}

const void *FC_StoreFile (const char *path, int kind, int *length)
{
	fc_writer_t	*w;
	const void	*cached;
	int			len, done, chunk;
	byte		*buf;

	len = FS_LoadFile ((char *)path, NULL);
	if (len <= 0 || !FC_Ready ())
		return NULL;
	w = Z_Malloc (sizeof(*w));
	if (!FC_WriteBegin (w, path, kind, len))
	{
		Z_Free (w);
		return NULL;
	}
	buf = Z_Malloc (16 * 1024);
	for (done = 0; done < len; done += chunk)
	{
		chunk = len - done < 16 * 1024 ? len - done : 16 * 1024;
		if (FS_LoadFileRange ((char *)path, done, chunk, buf) != chunk)
			break;
		FC_Write (w, buf, chunk);
	}
	Z_Free (buf);
	cached = FC_WriteEnd (w);
	Z_Free (w);
	if (cached && length)
		*length = len;
	return cached;
}

void FC_Pin (const char *name, int kind)
{
	int		i;

	if (!FC_Ready ())
		return;
	i = FC_Lookup (name, kind);
	if (i >= 0)
		dir[i].flags |= FCF_PINNED;
}

int FC_Owns (const void *p)
{
	return flash && (const byte *)p >= flash->base && (const byte *)p < flash->base + flash->size;
}

int FC_LevelOverflow (void)
{
	return overflow;
}

void FC_EvictAll (void)
{
	int		i;

	if (!FC_Ready ())
		return;
	for (i = 0; i < numentries; i++)
		if (dir[i].offset && !(dir[i].flags & FCF_PINNED))
		{
			FC_RemoveEntry (i);
			stats.evictions++;
		}
	overflow = false;
}

void FC_BeginLevel (const char *mapname)
{
	if (!FC_Ready ())
		return;
	overflow = false;
	if (!strcmp (mapname, cur_map))
		return;
	strncpy (cur_map, mapname, sizeof(cur_map) - 1);
	cur_seq++;
	dirty = true;
	level_stored = 0;
	level_notice = false;
}

// The bytes [from, from + len) of a directory slot as written: the header, then the
// entries with their RAM-only flags cleared, then 0xff. from and len are multiples
// of the entry size, which is the header size.
static void FC_SlotImage (byte *buf, unsigned from, unsigned len)
{
	fc_entry_t	*e = (fc_entry_t *)buf;
	unsigned	k;

	for (k = from / sizeof(*e); k < (from + len) / sizeof(*e); k++, e++)
	{
		if (k == 0)
			memcpy (e, &hdr, sizeof(hdr));
		else if (k <= (unsigned)numentries)
		{
			*e = dir[k - 1];
			e->flags = 0;
		}
		else
			memset (e, 0xff, sizeof(*e));
	}
}

void FC_Commit (void)
{
	int			i, n, slot;
	unsigned	bytes, done, chunk, c;
	fc_entry_t	e;
	byte		*buf;

	if (!FC_Ready () || !dirty)
		return;

	// drop unused slots at the end of the table
	n = numentries;
	while (n > 0 && !dir[n - 1].offset)
		n--;
	numentries = n;

	slot = active_slot ^ 1;
	hdr.magic = FC_MAGIC;
	hdr.version = FC_VERSION;
	hdr.generation++;
	hdr.num_entries = numentries;
	hdr.seq = cur_seq;

	// entries go to flash with their RAM-only flags cleared
	c = 0xffffffff;
	for (i = 0; i < numentries; i++)
	{
		e = dir[i];
		e.flags = 0;
		c = FC_CrcUpdate (c, &e, sizeof(e));
	}
	hdr.dir_crc = ~c;

	// The slot image is built FC_COMMIT_CHUNK at a time, since a commit can come in
	// the middle of a level's load, when PSRAM is short. Entries first, header page
	// last, so a torn write leaves the slot invalid.
	bytes = (sizeof(hdr) + numentries * sizeof(fc_entry_t) + FC_PAGE - 1) & ~(FC_PAGE - 1);
	buf = Z_Malloc (FC_COMMIT_CHUNK);
	flash->erase (slot * FC_SLOT_BYTES, FC_SLOT_BYTES);
	for (done = FC_PAGE; done < bytes; done += chunk)
	{
		chunk = bytes - done < FC_COMMIT_CHUNK ? bytes - done : FC_COMMIT_CHUNK;
		FC_SlotImage (buf, done, chunk);
		flash->program (slot * FC_SLOT_BYTES + done, buf, chunk);
	}
	FC_SlotImage (buf, 0, FC_PAGE);
	flash->program (slot * FC_SLOT_BYTES, buf, FC_PAGE);
	stats.bytes_erased += FC_SLOT_BYTES;
	stats.bytes_programmed += bytes;
	Z_Free (buf);

	active_slot = slot;
	dirty = false;
	uncommitted = 0;
}

void FC_GetStats (fc_stats_t *st)
{
	int		i;

	*st = stats;
	st->entries = 0;
	if (!flash || !loaded)
		return;
	for (i = 0; i < numentries; i++)
		if (dir[i].offset)
			st->entries++;
	st->free_bytes = st->capacity - st->used_bytes;
}
