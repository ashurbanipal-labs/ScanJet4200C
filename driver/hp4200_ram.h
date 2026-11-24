#ifndef HP4200_RAM_H
#define HP4200_RAM_H

#include "hp4200_io.h"

typedef struct hp4200_ram_diagnostic {
    unsigned channel, shading, attempt;
    size_t offset, last_offset, mismatch_count;
    size_t zero_bytes, matches_previous, matches_next, matches_swapped;
    uint8_t expected, actual;
    int has_mismatch;
    unsigned write_retries;
} hp4200_ram_diagnostic;

/* HP InitializeScanner FUN_10006410 and FUN_100064d0. Verifies both the
 * three 1024-byte color tables and three 10,920-byte shading RAM planes.
 * Requires the factory register image and bridge to be initialized first. */
int hp4200_selftest_ram(const hp4200_io *io);
int hp4200_selftest_ram_diagnose(const hp4200_io *io,
                                 hp4200_ram_diagnostic *diagnostic);

/* The PV8630 has reported a complete USB write while retaining a previous
 * ASIC plane. Check each final shading/lookup upload before scanning, with
 * one bounded rewrite after a persistent mismatch. */
int hp4200_write_verified_plane(const hp4200_io *io, unsigned channel,
                                int shading, const uint8_t *data,
                                size_t length);

#endif
