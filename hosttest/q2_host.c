/*
 * Host harness for pico-quake2: runs the vendored engine headless on Linux.
 *
 *   q2_host <basedir> <frames> <dump-every-N> <outdir> [+quake2 commands...] [@N command...]
 *
 * "@N command" runs a console command at frame N (for example "@600 map demo2"),
 * so level transitions can be scripted.
 *
 * The clock is virtual (a fixed number of ms per frame), so runs are deterministic
 * and as fast as the host allows. Frames are written as PPM through the current
 * palette; the mixer runs against a fake DMA buffer so the sound path is exercised.
 *
 * The engine heap is an arena of what the device's 8 MB PSRAM leaves after the
 * engine's static data, so running out here means running out on the board.
 * Q2_SYSTEM_MALLOC=1 uses the system allocator instead (ASan sees heap overflows,
 * but there is no budget). Q2_HEAP_KB=<n> overrides the heap size, to measure what
 * a run would need.
 *
 * The flash asset cache gets an emulated NOR region of Q2_FLASH_KB (default 13312,
 * the region after a 2.5 MB app slot on a 16 MB board; 0 disables the cache). Q2_FLASH_FILE=<path> keeps the region in a file between
 * runs, so a second run shows a warm cache. Q2_FLASH_CLOCK=<factor> lets the virtual
 * clock run during flash writes (their typical time times factor), so a cold level
 * load takes as long as on the board. Q2_FLASH_STOP_KB=<n> ends the run after n KB
 * programmed, as a reset in the middle of a cache fill. Q2_FRAME_MS=<n> sets the
 * virtual time per frame (default 16).
 *
 * Q2_STACK=1 (builds without sanitizers) reports how deep the engine used the stack:
 * a region below main's frame is painted before the run and scanned afterwards.
 *
 * q2_host_sdl is this file built with -DQ2_HOST_SDL, plus q2_sdl.c: the same run in a
 * window with keyboard, mouse and sound, on real time. <frames> 0 runs until the window
 * is closed; there <frames> and @N count drawn frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "ref_soft/r_local.h"
#include "client/snd_loc.h"
#include "quake2.h"
#include "quakegeneric.h"
#include "q_mem.h"
#include "qcommon/fcache.h"

void Cbuf_AddText (char *text);

#ifdef Q2_HOST_SDL
/* q2_sdl.c: the window; it also has the input, sound and platform config hooks */
int q2_sdl_init(void);
int q2_sdl_poll(void);
int q2_sdl_ticks(void);
void q2_sdl_present(const byte *fb, byte palette[256][3]);
#endif

#ifndef Q2_ENGINE_STATIC_BYTES
#define Q2_ENGINE_STATIC_BYTES 0
#endif
#define PSRAM_BYTES (8 * 1024 * 1024)

#define FRAME_MS 16	/* Q2_FRAME_MS overrides it */
static int frame_ms = FRAME_MS;

static int vtime;	/* the virtual clock; with SDL only the flash time (Q2_FLASH_CLOCK) */
static int frame_no;
static int dump_every;
static const char *outdir;

static byte fb[320 * 240];
static byte palette[256][3];

/* ---- quakegeneric platform hooks ---- */

#ifdef Q2_HOST_SDL
int QG_Milliseconds(void) { return q2_sdl_ticks() + vtime; }
#else
int QG_Milliseconds(void) { return vtime; }
void QG_GetMouseDiff(int *dx, int *dy) { *dx = *dy = 0; }
void QG_CaptureMouse(void) {}
void QG_ReleaseMouse(void) {}
const char *QG_PlatformConfig(void) { return ""; }
#endif
void QG_Mkdir(const char *path) { mkdir(path, 0777); }
const char *QG_ConsoleInput(void) { return NULL; }

/* directory listing: opendir + fnmatch on the last path component */
static DIR *find_dir;
static char find_base[512], find_pat[128], find_path[640];

const char *QG_FindNext(int *isdir)
{
	struct dirent *d;
	struct stat st;

	if (!find_dir)
		return NULL;
	while ((d = readdir(find_dir))) {
		if (fnmatch(find_pat, d->d_name, 0))
			continue;
		snprintf(find_path, sizeof(find_path), "%s/%s", find_base, d->d_name);
		*isdir = !stat(find_path, &st) && S_ISDIR(st.st_mode);
		return find_path;
	}
	return NULL;
}

const char *QG_FindFirst(const char *pattern, int *isdir)
{
	const char *slash = strrchr(pattern, '/');

	if (find_dir)
		closedir(find_dir);
	if (slash) {
		snprintf(find_base, sizeof(find_base), "%.*s", (int)(slash - pattern), pattern);
		snprintf(find_pat, sizeof(find_pat), "%s", slash + 1);
	} else {
		snprintf(find_base, sizeof(find_base), ".");
		snprintf(find_pat, sizeof(find_pat), "%s", pattern);
	}
	find_dir = opendir(find_base);
	return QG_FindNext(isdir);
}

void QG_FindClose(void)
{
	if (find_dir)
		closedir(find_dir);
	find_dir = NULL;
}

/* ---- SWimp: 320x240 8bpp framebuffer, PPM dumps ---- */

int SWimp_Init(void *hInstance, void *wndProc)
{
	vid.buffer = fb;
	vid.rowbytes = 320;
	return true;
}

rserr_t SWimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
	*pwidth = 320;
	*pheight = 240;
	vid.buffer = fb;
	vid.rowbytes = 320;
	ri.Vid_NewWindow(320, 240);
	return rserr_ok;
}

void SWimp_Shutdown(void) {}
void SWimp_AppActivate(qboolean active) {}
void SWimp_BeginFrame(float camera_separation) {}

void SWimp_SetPalette(const unsigned char *pal)
{
	if (!pal)
		return;
	for (int i = 0; i < 256; i++) {
		palette[i][0] = pal[i * 4 + 0];
		palette[i][1] = pal[i * 4 + 1];
		palette[i][2] = pal[i * 4 + 2];
	}
}

static void dump_ppm(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/frame_%05d.ppm", outdir, frame_no);
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n320 240\n255\n");
	for (int i = 0; i < 320 * 240; i++)
		fwrite(palette[fb[i]], 1, 3, f);
	fclose(f);
}

void SWimp_WaitBuffer(void) {}

void SWimp_EndFrame(void)
{
	frame_no++;
	if (dump_every > 0 && frame_no % dump_every == 0)
		dump_ppm();
#ifdef Q2_HOST_SDL
	q2_sdl_present(fb, palette);
#endif
}

#ifndef Q2_HOST_SDL
/* ---- SNDDMA: fake 22050 Hz stereo ring that advances with the virtual clock ---- */

#define DMA_SAMPLES 8192 /* mono samples (stereo pairs * 2), power of two */
static short dma_buffer[DMA_SAMPLES];

qboolean SNDDMA_Init(void)
{
	memset(&dma, 0, sizeof(dma));
	dma.channels = 2;
	dma.samplebits = 16;
	dma.speed = 22050;
	dma.samples = DMA_SAMPLES;
	dma.submission_chunk = 1;
	dma.buffer = (byte *)dma_buffer;
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	long long pairs = (long long)vtime * dma.speed / 1000;
	return (int)((pairs * dma.channels) & (dma.samples - 1));
}

void SNDDMA_Shutdown(void) {}
void SNDDMA_BeginPainting(void) {}

/* Peak amplitude seen in the mixed output, to confirm the mixer is not silent. */
static int audio_peak;
void SNDDMA_Submit(void)
{
	for (int i = 0; i < DMA_SAMPLES; i++) {
		int a = dma_buffer[i] < 0 ? -dma_buffer[i] : dma_buffer[i];
		if (a > audio_peak)
			audio_peak = a;
	}
}
#endif

/* ---- flash: emulated NOR region for the asset cache ---- */

static fc_flash_t flash;
static double flash_ms;		/* estimated time of erase/program operations */
static double flash_clock;	/* Q2_FLASH_CLOCK: the virtual clock advances by flash_ms times this */
static double flash_clock_ms;	/* the part not yet added to vtime */
static unsigned long long flash_stop;	/* Q2_FLASH_STOP_KB, in bytes; 0 = never */
static unsigned long long flash_programmed;

static void flash_time(double ms)
{
	flash_ms += ms;
	if (flash_clock > 0) {
		flash_clock_ms += ms * flash_clock;
		vtime += (int)flash_clock_ms;
		flash_clock_ms -= (int)flash_clock_ms;
	}
}

static void flash_erase(unsigned offset, unsigned len)
{
	if (offset % FC_SECTOR || len % FC_SECTOR || offset + len > flash.size) {
		fprintf(stderr, "flash_erase: bad range %u+%u\n", offset, len);
		abort();
	}
	memset(flash.base + offset, 0xff, len);
	/* W25Q128-class typical timings: 64 KB block 150 ms, 4 KB sector 45 ms */
	while (len) {
		if (offset % 65536 == 0 && len >= 65536) {
			flash_time(150); offset += 65536; len -= 65536;
		} else {
			flash_time(45); offset += FC_SECTOR; len -= FC_SECTOR;
		}
	}
}

static void flash_program(unsigned offset, const void *data, unsigned len)
{
	if (offset % FC_PAGE || len % FC_PAGE || offset + len > flash.size) {
		fprintf(stderr, "flash_program: bad range %u+%u\n", offset, len);
		abort();
	}
	const byte *src = data;
	for (unsigned i = 0; i < len; i++)
		flash.base[offset + i] &= src[i];	/* NOR: programming only clears bits */
	flash_time(0.4 * (len / FC_PAGE));	/* typical page program time */

	/* a reset in the middle of a cache fill: what was programmed stays in Q2_FLASH_FILE */
	flash_programmed += len;
	if (flash_stop && flash_programmed >= flash_stop) {
		printf("q2_host: stopped after %llu KB programmed (Q2_FLASH_STOP_KB), at %d ms\n",
			flash_programmed / 1024, QG_Milliseconds());
		fflush(stdout);
		_exit(0);
	}
}

static void flash_setup(void)
{
	const char *kb = getenv("Q2_FLASH_KB");
	unsigned size = (kb ? (unsigned)atoi(kb) : 13312) * 1024u;
	const char *file = getenv("Q2_FLASH_FILE");
	const char *clk = getenv("Q2_FLASH_CLOCK");
	const char *stop = getenv("Q2_FLASH_STOP_KB");

	if (!size)
		return;
	flash_clock = clk ? atof(clk) : 0;
	flash_stop = stop ? strtoull(stop, NULL, 10) * 1024 : 0;
	if (file) {
		int fd = open(file, O_RDWR | O_CREAT, 0644);
		struct stat st;
		if (fd < 0 || fstat(fd, &st)) {
			perror(file);
			exit(1);
		}
		int fresh = (unsigned)st.st_size != size;
		if (fresh && ftruncate(fd, size)) {
			perror(file);
			exit(1);
		}
		flash.base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		close(fd);
		if (fresh)
			memset(flash.base, 0xff, size);
	} else {
		flash.base = malloc(size);
		memset(flash.base, 0xff, size);
	}
	flash.size = size;
	flash.erase = flash_erase;
	flash.program = flash_program;
	FC_SetFlash(&flash);
}

/* ---- stack high-water ---- */

#define STACK_PAINT (1024 * 1024)
static volatile unsigned char *stack_low;

__attribute__((noinline)) static void stack_paint(void)
{
	volatile unsigned char probe[STACK_PAINT];
	for (int i = 0; i < STACK_PAINT; i++)
		probe[i] = 0xa5;
	stack_low = probe;
}

__attribute__((noinline)) static size_t stack_used(volatile unsigned char *top)
{
	for (volatile unsigned char *p = stack_low; p < top; p++)
		if (*p != 0xa5)
			return (size_t)(top - p);
	return 0;
}

/* ---- main ---- */

/* "@N command" from the command line */
static struct { int frame; char cmd[256]; } sched[32];
static int nsched;

static void run_scheduled(int frame)
{
	for (int s = 0; s < nsched; s++)
		if (sched[s].frame == frame) {
			qmem_stats_t st;
			Qmem_GetStats(&st);
			printf("q2_host: frame %d: '%.*s' (heap peak so far %zu KB)\n", frame,
				(int)strlen(sched[s].cmd) - 1, sched[s].cmd, st.peak / 1024);
			Cbuf_AddText(sched[s].cmd);
		}
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: %s <basedir> <frames> <dump-every-N> <outdir> [+cmds...]\n", argv[0]);
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);	/* Sys_Error exits with _Exit */
	const char *basedir = argv[1];
	int frames = atoi(argv[2]);
	dump_every = atoi(argv[3]);
	outdir = argv[4];
	mkdir(outdir, 0777);

	/* Engine argv: program name, +set basedir, then the caller's commands. */
	char *qargv[64];
	int qargc = 0;
	qargv[qargc++] = argv[0];
	qargv[qargc++] = "+set";
	qargv[qargc++] = "basedir";
	qargv[qargc++] = (char *)basedir;
	for (int i = 5; i < argc && qargc < 63; i++) {
		if (argv[i][0] == '@' && i + 1 < argc && nsched < 32) {
			sched[nsched].frame = atoi(argv[i] + 1);
			snprintf(sched[nsched].cmd, sizeof(sched[nsched].cmd), "%s\n", argv[i + 1]);
			nsched++;
			i++;
			continue;
		}
		qargv[qargc++] = argv[i];
	}
	qargv[qargc] = NULL;

	size_t heap_bytes = PSRAM_BYTES > Q2_ENGINE_STATIC_BYTES ? (PSRAM_BYTES - Q2_ENGINE_STATIC_BYTES) & ~(size_t)63 : 0;
	if (getenv("Q2_HEAP_KB"))
		heap_bytes = (size_t)atol(getenv("Q2_HEAP_KB")) * 1024;
	if (getenv("Q2_SYSTEM_MALLOC")) {
		Qmem_Init(NULL, 0);
		printf("q2_host: system allocator (no PSRAM budget)\n");
	} else {
		if (heap_bytes < 1024 * 1024) {
			fprintf(stderr, "q2_host: engine static data (%d bytes) leaves no PSRAM heap\n", Q2_ENGINE_STATIC_BYTES);
			return 1;
		}
		Qmem_Init(aligned_alloc(64, heap_bytes), heap_bytes);
		printf("q2_host: PSRAM model: %d KB static + %zu KB heap\n", Q2_ENGINE_STATIC_BYTES / 1024, heap_bytes / 1024);
	}

	flash_setup();
#ifdef Q2_HOST_SDL
	if (!q2_sdl_init())
		return 1;
#endif

	volatile unsigned char stack_top;
	int stack_check = getenv("Q2_STACK") != NULL;
	if (stack_check)
		stack_paint();

	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	vtime = 1;
	Quake2_Init(qargc, qargv);

#ifdef Q2_HOST_SDL
	/* Real time, as the board's main loop; the engine holds the frame rate to
	 * cl_maxfps. Runs until the window is closed or <frames> frames are drawn. */
	int last = QG_Milliseconds(), sched_frame = -1;
	while ((frames <= 0 || frame_no < frames) && q2_sdl_poll()) {
		if (frame_no != sched_frame) {
			run_scheduled(frame_no);
			sched_frame = frame_no;
		}
		int now = QG_Milliseconds();
		if (now - last < 1) {
			usleep(1000);
			continue;
		}
		Quake2_Frame(now - last);
		last = now;
	}
#else
	/* Each frame gets the virtual time since the previous one: frame_ms, plus the
	 * flash time of a cache fill with Q2_FLASH_CLOCK, which then also gets a line
	 * every 5 virtual seconds, as the board's status line. */
	if (getenv("Q2_FRAME_MS"))
		frame_ms = atoi(getenv("Q2_FRAME_MS"));
	int last = vtime, status_t = vtime, status_frames = frame_no;
	for (int i = 0; i < frames; i++) {
		run_scheduled(i);
		vtime += frame_ms;
		int msec = vtime - last;
		last = vtime;
		Quake2_Frame(msec);
		if (flash_clock > 0 && vtime - status_t >= 5000) {
			printf("q2_host: t=%d s, %d frames drawn in %d ms\n", vtime / 1000,
				frame_no - status_frames, vtime - status_t);
			status_t = vtime;
			status_frames = frame_no;
		}
	}
#endif

	clock_gettime(CLOCK_MONOTONIC, &t1);
	double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
#ifdef Q2_HOST_SDL
	printf("q2_host: %d frames rendered in %.2f s wall\n", frame_no, secs);
#else
	printf("q2_host: %d frames rendered in %.2f s wall, audio peak %d\n", frame_no, secs, audio_peak);
#endif

	if (stack_check)
		printf("q2_host: stack high-water %zu bytes below main\n", stack_used(&stack_top));
	printf("q2_host: render work: edges %d/%d, surfs %d/%d, spans %d/%d (%d early flushes), "
		"finalverts %d/%d, frames short of edges %d, of surfs %d\n",
		r_workstats.edges, NUMSTACKEDGES, r_workstats.surfs, NUMSTACKSURFACES,
		r_workstats.spans, MAXSPANS, r_workstats.span_flushes,
		r_workstats.finalverts, MAXALIASVERTS, r_workstats.short_edges, r_workstats.short_surfs);

	Qmem_DebugDumpLive();
	qmem_stats_t st;
	Qmem_GetStats(&st);
	printf("q2_host: heap peak %zu KB, in use %zu KB (%u blocks), largest free %zu KB\n",
		st.peak / 1024, st.used / 1024, st.live_blocks, st.largest_free / 1024);
	printf("q2_host: PSRAM peak %zu KB of %d KB\n", (Q2_ENGINE_STATIC_BYTES + st.peak) / 1024, PSRAM_BYTES / 1024);

	if (flash.size) {
		fc_stats_t fc;
		FC_GetStats(&fc);
		printf("q2_host: flash cache: %u assets, %u KB used of %u KB; %u hits, %u misses, %u stored, "
			"%u store failures, %u evictions\n", fc.entries, fc.used_bytes / 1024, fc.capacity / 1024,
			fc.hits, fc.misses, fc.stores, fc.store_failures, fc.evictions);
		printf("q2_host: flash writes: %u KB erased, %u KB programmed, ~%.1f s on the device\n",
			fc.bytes_erased / 1024, fc.bytes_programmed / 1024, flash_ms / 1000);
	}
	return 0;
}
