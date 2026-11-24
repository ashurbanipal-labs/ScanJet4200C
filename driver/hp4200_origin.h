#ifndef HP4200_ORIGIN_H
#define HP4200_ORIGIN_H

#include <stddef.h>
#include <stdint.h>

/* FUN_10006850: locate HP's reference marker in the 150-line origin scan.
 * Each line has one status byte followed by interleaved RGB samples. The
 * initial y value is retained when HP's minimum vertical origin rule applies. */
int hp4200_find_origin(const uint8_t *image, size_t line_stride,
                       size_t line_count, uint16_t factory_x,
                       uint16_t initial_y, uint16_t *origin_x,
                       uint16_t *origin_y);

#endif
