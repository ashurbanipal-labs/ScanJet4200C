#include "hp4200_pipeline.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    hp4200_pipeline pipeline;
    uint8_t raw[30], output[12];
    assert(hp4200_pipeline_init(&pipeline, 10, 4, 1, NULL) == 0);
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned x = 0; x < 10; ++x) {
            raw[x * 3] = (uint8_t)(row * 10 + x);
            raw[x * 3 + 1] = (uint8_t)(row * 10 + x + 40);
            raw[x * 3 + 2] = (uint8_t)(row * 10 + x + 80);
        }
        int ready = hp4200_pipeline_push(&pipeline, raw, sizeof(raw),
                                          output, sizeof(output));
        if (row < 2) assert(ready == 0);
        else {
            assert(ready == 1);
            assert(output[0] == (row - 2) * 10);
            assert(output[1] == (row - 1) * 10 + 40);
            assert(output[2] == row * 10 + 80);
            assert(output[3] == (row - 2) * 10 + 2);
        }
    }
    hp4200_pipeline_destroy(&pipeline);
    puts("HP combined pixel pipeline tests passed");
    return 0;
}
