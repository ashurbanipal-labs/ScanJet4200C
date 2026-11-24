#ifndef HP4200_SQUARE_PIXELS_H
#define HP4200_SQUARE_PIXELS_H

#include <stddef.h>
#include <stdint.h>

/* Keep each measured horizontal RGB sample and insert one interpolated
 * display sample. The scanner still measures only the original pixels. */
void hp4200_expand_horizontal_2x(const uint8_t *source, size_t pixels,
                                  uint8_t *destination);

#endif
