#include "hp4200_rows.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    hp4200_rows rows;
    uint8_t raw[6], output[6];
    assert(hp4200_rows_init(&rows, 2, 4) == 0);
    for (unsigned line = 0; line < 11; ++line) {
        for (unsigned pixel = 0; pixel < 2; ++pixel) {
            raw[pixel * 3] = (uint8_t)line;
            raw[pixel * 3 + 1] = (uint8_t)(line + 40);
            raw[pixel * 3 + 2] = (uint8_t)(line + 80);
        }
        int ready = hp4200_rows_push(&rows, raw, output);
        if (line < 8) assert(ready == 0);
        else {
            assert(ready == 1);
            assert(output[0] == line - 8);
            assert(output[1] == line - 4 + 40);
            assert(output[2] == line + 80);
        }
    }
    assert(rows.output_rows == 3);
    hp4200_rows_destroy(&rows);
    puts("HP staggered row tests passed");
    return 0;
}
