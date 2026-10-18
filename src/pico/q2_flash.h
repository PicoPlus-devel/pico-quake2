// q2_flash.h -- flash access for the asset cache (see q2_flash.c).

#ifndef Q2_FLASH_H
#define Q2_FLASH_H

// Reads the flash chip's JEDEC ID. Call first in main(), before the overclock
// and the PSRAM setup.
void	q2_flash_probe (void);

// Hands the region Q2_FCACHE_BASE..+Q2_FCACHE_SIZE to the asset cache, cut to
// the size of the chip q2_flash_probe found.
void	q2_flash_init (void);

// Once per frame: switches the board LED, which blinks while assets are written
// to flash, off again when the writes have stopped.
void	q2_flash_idle (void);

#endif
