#include "hp4200_pixels.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    uint8_t mask[10];
    uint8_t line[30];
    hp4200_color_transform transform = {0};
    assert(hp4200_sample_mask(mask, 10, 4) == 4);
    for (size_t i = 0; i < 10; ++i) {
        line[i * 3] = line[i * 3 + 1] = line[i * 3 + 2] = (uint8_t)i;
    }
    assert(hp4200_compact_rgb(line, 10, mask) == 12);
    assert(line[0] == 0 && line[3] == 2 && line[6] == 5 && line[9] == 7);
    assert(hp4200_sample_mask(mask, 10, 10) == 10);
    for (size_t i = 0; i < 10; ++i) assert(mask[i] == 1);
    assert(hp4200_sample_mask(mask, 10, 11) == 0);
    for (size_t raw = 1; raw <= 10; ++raw)
        for (size_t wanted = 1; wanted <= raw; ++wanted)
            assert(hp4200_sample_mask(mask, raw, wanted) == wanted);
    hp4200_color_matrix(&transform, 0);
    assert(transform.coefficient[0][0] == 0x33c1);
    assert(transform.coefficient[2][2] == 0x2eba);
    hp4200_color_matrix(&transform, 1);
    assert(transform.coefficient[1][1] == 0x333e);
    for (unsigned channel = 0; channel < 3; ++channel)
        for (unsigned source = 0; source < 3; ++source)
            transform.coefficient[channel][source] = 0;
    for (unsigned channel = 0; channel < 3; ++channel) {
        transform.coefficient[channel][channel] = 10000;
        for (unsigned index = 0; index < 1024; ++index)
            transform.table[channel][index] = (uint8_t)(index / 4);
    }
    uint8_t pixel[3] = {12, 80, 255};
    hp4200_transform_rgb(pixel, 1, &transform);
    assert(pixel[0] == 12 && pixel[1] == 80 && pixel[2] == 255);
    puts("HP pixel path tests passed");
    return 0;
}
