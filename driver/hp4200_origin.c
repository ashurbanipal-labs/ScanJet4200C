#include "hp4200_origin.h"

int hp4200_find_origin(const uint8_t *image, size_t line_stride,
                       size_t line_count, uint16_t factory_x,
                       uint16_t initial_y, uint16_t *origin_x,
                       uint16_t *origin_y)
{
    unsigned reference = 0, threshold;
    size_t row = 10, col = 50, pixels;
    unsigned adjustment;
    if (!image || !origin_x || !origin_y || line_stride < 154 ||
        line_count < 16 || line_count > SIZE_MAX / line_stride) return -1;
    pixels = (line_stride - 1) / 3;
    if (pixels <= 50) return -1;
    /* DLL offset 0x97 is status byte 1 + 50 RGB pixels. */
    for (size_t i = 0; i < 10; ++i)
        reference += image[i * line_stride + 0x97];
    threshold = (reference / 10) / 3;
    if (threshold <= 9) return -1;
    while (row < line_count && image[row * line_stride + 0x97] > threshold)
        ++row;
    if (row + 5 >= line_count) return -1;
    while (col < pixels &&
           image[(row + 5) * line_stride + 1 + col * 3] < threshold)
        ++col;
    if (col == pixels) return -1;
    adjustment = (unsigned)(col * 2);
    if (adjustment < 140 || adjustment > 300) adjustment = 0;
    else adjustment -= 140;
    if (factory_x > UINT16_MAX - adjustment || row + 20 > UINT16_MAX)
        return -1;
    *origin_x = (uint16_t)(factory_x + adjustment);
    *origin_y = row + 20 > 25 ? (uint16_t)(row + 20) : initial_y;
    return 0;
}
