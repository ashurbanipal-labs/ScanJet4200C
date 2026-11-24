#include "hp4200_square_pixels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t measured[] = {10, 20, 30, 11, 21, 31, 255, 0, 100};
    const uint8_t expected[] = {
        10, 20, 30, 11, 21, 31,
        11, 21, 31, 133, 11, 66,
        255, 0, 100, 255, 0, 100
    };
    uint8_t expanded[sizeof(expected)] = {0};
    hp4200_expand_horizontal_2x(measured, 3, expanded);
    assert(memcmp(expanded, expected, sizeof(expected)) == 0);
    puts("HP square-pixel export test passed");
    return 0;
}
