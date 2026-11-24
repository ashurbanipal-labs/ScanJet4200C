#include "hp4200_origin.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    enum { STRIDE = 1 + 100 * 3, HEIGHT = 150 };
    uint8_t image[STRIDE * HEIGHT];
    uint16_t x, y;
    memset(image, 80, sizeof(image));
    for (unsigned col = 0; col < 80; ++col)
        image[26 * STRIDE + 1 + col * 3] = 0;
    image[21 * STRIDE + 0x97] = 0;
    assert(hp4200_find_origin(image, STRIDE, HEIGHT, 100, 25, &x, &y) == 0);
    assert(x == 120 && y == 41);
    memset(image, 0, sizeof(image));
    assert(hp4200_find_origin(image, STRIDE, HEIGHT, 100, 25, &x, &y) == -1);
    puts("HP origin marker tests passed");
    return 0;
}
