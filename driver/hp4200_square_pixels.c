#include "hp4200_square_pixels.h"

void hp4200_expand_horizontal_2x(const uint8_t *source, size_t pixels,
                                  uint8_t *destination)
{
    for (size_t pixel = 0; pixel < pixels; ++pixel)
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned first = source[pixel * 3 + channel];
            unsigned second = source[(pixel + (pixel + 1 < pixels)) * 3 +
                                     channel];
            destination[pixel * 6 + channel] = (uint8_t)first;
            destination[pixel * 6 + 3 + channel] =
                (uint8_t)((first + second + 1) / 2);
        }
}
