#include "hp4200_pixels.h"

#include <string.h>

size_t hp4200_sample_mask(uint8_t *mask, size_t raw_pixels,
                          size_t output_pixels)
{
    size_t accumulator, remaining, selected = 0;
    if (!mask || raw_pixels == 0 || output_pixels == 0 ||
        output_pixels > raw_pixels || raw_pixels > 65535) return 0;
    memset(mask, 0, raw_pixels);
    mask[0] = 1;
    accumulator = raw_pixels;
    remaining = output_pixels - 1;
    /* Direct translation of FUN_10001340's remainder distribution. */
    for (size_t i = 0; i < raw_pixels; ++i) {
        while (accumulator < output_pixels) {
            if (remaining == 0) return 0;
            --remaining;
            accumulator += raw_pixels;
            ++mask[i];
        }
        accumulator -= output_pixels;
    }
    mask[raw_pixels - 1] = (uint8_t)(mask[raw_pixels - 1] + remaining);
    for (size_t i = 0; i < raw_pixels; ++i) selected += mask[i] != 0;
    return selected;
}

size_t hp4200_compact_rgb(uint8_t *rgb, size_t raw_pixels,
                          const uint8_t *mask)
{
    size_t output = 0;
    if (!rgb || !mask) return 0;
    for (size_t i = 0; i < raw_pixels; ++i) {
        if (mask[i]) {
            memmove(rgb + output * 3, rgb + i * 3, 3);
            ++output;
        }
    }
    return output * 3;
}

static unsigned clamp_index(int64_t value)
{
    if (value < 0) return 0;
    if (value > 1023) return 1023;
    return (unsigned)value;
}

void hp4200_color_matrix(hp4200_color_transform *transform,
                          int special_sensor_mode)
{
    static const int32_t ordinary[3][3] = {
        {0x33c1, -0x628, -0x688},
        {-0x533, 0x2d61, -0x11e},
        {-0x1b9, -0x5f1, 0x2eba},
    };
    static const int32_t special[3][3] = {
        {0x3f73, -0xee4, -0x97d},
        {-0x547, 0x333e, -0x6e8},
        {-0xae, -0xff9, 0x37b7},
    };
    if (!transform) return;
    memcpy(transform->coefficient,
           special_sensor_mode ? special : ordinary,
           sizeof(transform->coefficient));
}

void hp4200_transform_rgb(uint8_t *rgb, size_t pixels,
                          const hp4200_color_transform *transform)
{
    if (!rgb || !transform) return;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        uint8_t input[3] = {rgb[pixel * 3], rgb[pixel * 3 + 1],
                            rgb[pixel * 3 + 2]};
        for (unsigned channel = 0; channel < 3; ++channel) {
            int64_t sum = 0;
            for (unsigned source = 0; source < 3; ++source)
                sum += (int64_t)input[source] *
                       transform->coefficient[channel][source];
            rgb[pixel * 3 + channel] =
                transform->table[channel][clamp_index(sum / 2500)];
        }
    }
}
