#ifndef HP4200_INITIALIZE_H
#define HP4200_INITIALIZE_H

#include "hp4200_io.h"
#include "hp4200_ram.h"

/* FUN_10005c80 + FUN_10005d10: write and verify the factory LM9830
 * register image. This is one phase of HP InitializeScanner, not the full
 * home/lamp/calibration sequence. Do not call alone for a hardware scan. */
int hp4200_initialize_register_image(const hp4200_io *io);

/* FUN_10005db0: the short 0x0b..0x0d register probe used on reuse. */
int hp4200_probe_core_registers(const hp4200_io *io);

/* HP InitializeScanner -> FUN_10006590 first-use path: bridge activation,
 * factory registers, home cycle, and ASIC RAM checks. This moves the head. */
int hp4200_initialize_core(const hp4200_io *io);

/* The DLL starts in 16-bit sensor mode and retries in 8-bit mode when its
 * first lamp calibration line averages zero. */
int hp4200_initialize_core_mode(const hp4200_io *io, unsigned sensor_bits);
int hp4200_initialize_core_mode_diagnose(
    const hp4200_io *io, unsigned sensor_bits,
    hp4200_ram_diagnostic *diagnostic);

/* A fresh CLI process repeats HP's first-use RAM check on every job. If
 * readback mismatches once, repeat the complete checked initialization one
 * time after the scanner has settled; never bypass the RAM comparison. */
int hp4200_initialize_core_mode_with_ram_retry(
    const hp4200_io *io, unsigned sensor_bits,
    hp4200_ram_diagnostic *diagnostic, unsigned *retries);

#endif
