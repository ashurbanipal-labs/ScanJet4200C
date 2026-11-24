#include "hp4200_rows.h"
#include <stdlib.h>
#include <string.h>

int hp4200_rows_init(hp4200_rows *rows, size_t pixels,
                      unsigned separation)
{
    if (!rows || !pixels || pixels > 10000 || separation == 0 ||
        separation > 16) return -1;
    memset(rows, 0, sizeof(*rows));
    rows->pixels = pixels;
    rows->separation = separation;
    rows->slots = separation * 2 + 1;
    rows->storage = calloc((size_t)rows->slots, pixels * 3);
    return rows->storage ? 0 : -1;
}

void hp4200_rows_destroy(hp4200_rows *rows)
{
    if (!rows) return;
    free(rows->storage);
    memset(rows, 0, sizeof(*rows));
}

int hp4200_rows_push(hp4200_rows *rows, const uint8_t *raw_rgb,
                      uint8_t *output_rgb)
{
    size_t line_bytes, current, red, green, blue;
    if (!rows || !rows->storage || !raw_rgb || !output_rgb) return -1;
    line_bytes = rows->pixels * 3;
    current = rows->input_rows;
    memcpy(rows->storage + (current % rows->slots) * line_bytes,
           raw_rgb, line_bytes);
    ++rows->input_rows;
    if (current < rows->separation * 2) return 0;
    red = (current - rows->separation * 2) % rows->slots;
    green = (current - rows->separation) % rows->slots;
    blue = current % rows->slots;
    for (size_t pixel = 0; pixel < rows->pixels; ++pixel) {
        output_rgb[pixel * 3] = rows->storage[red * line_bytes + pixel * 3];
        output_rgb[pixel * 3 + 1] =
            rows->storage[green * line_bytes + pixel * 3 + 1];
        output_rgb[pixel * 3 + 2] =
            rows->storage[blue * line_bytes + pixel * 3 + 2];
    }
    ++rows->output_rows;
    return 1;
}
