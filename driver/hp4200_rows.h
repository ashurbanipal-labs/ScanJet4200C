#ifndef HP4200_ROWS_H
#define HP4200_ROWS_H

#include <stddef.h>
#include <stdint.h>

/* The HP DLL's color ring uses two equal stagger distances (FUN_100035c0,
 * FUN_10001130, FUN_10002540). For the 4200C they are four raw lines at
 * 600 dpi, giving R/G/B source rows i-8/i-4/i. */
typedef struct {
    uint8_t *storage;
    size_t pixels;
    unsigned separation;
    unsigned slots;
    size_t input_rows;
    size_t output_rows;
} hp4200_rows;

int hp4200_rows_init(hp4200_rows *rows, size_t pixels,
                      unsigned separation);
void hp4200_rows_destroy(hp4200_rows *rows);
/* Input is one selected RGB raw line; output must hold pixels*3 bytes.
 * Returns 1 when a complete output row is written, 0 during initial
 * stagger fill, -1 on invalid input. */
int hp4200_rows_push(hp4200_rows *rows, const uint8_t *raw_rgb,
                      uint8_t *output_rgb);

#endif
