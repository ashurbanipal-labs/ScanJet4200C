#include "hp4200_offset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill(uint8_t samples[96], unsigned value)
{
    for (unsigned i = 0; i < 48; ++i) {
        samples[i * 2] = (uint8_t)(value >> 8);
        samples[i * 2 + 1] = (uint8_t)value;
    }
}

int main(void)
{
    hp4200_offset_result result = {0};
    uint8_t samples[96];
    fill(samples, 1);
    assert(hp4200_offset_observe(&result, 0, samples, sizeof(samples), 0) == 0);
    for (unsigned c = 0; c < 3; ++c)
        assert(result.code[c] == 0x20 && result.positive_reference[c] == 0);
    hp4200_offset_prepare(&result, 1);
    assert(hp4200_offset_observe(&result, 1, samples, sizeof(samples), 0) == 0);
    assert(result.code[0] == 0x30);
    memset(samples, 0, sizeof(samples));
    for (unsigned pass = 2; pass <= 5; ++pass) {
        hp4200_offset_prepare(&result, pass);
        assert(hp4200_offset_observe(&result, pass, samples, sizeof(samples), 0) == 0);
    }
    assert(result.code[0] == 0x2f);
    fill(samples, 16);
    for (unsigned pass = 6; pass <= 10; ++pass)
        assert(hp4200_offset_observe(&result, pass, samples, sizeof(samples), 0) == 0);
    assert(result.measurements == 5);
    for (unsigned c = 0; c < 3; ++c) {
        assert(result.black_level[c] == 16 && result.shading_black[c] == 4);
        assert(result.final_reference[c] == (c == 2 ? 7 : 6));
    }
    fill(samples, 0x00ff);
    hp4200_offset_result special = {0};
    assert(hp4200_offset_observe(&special, 0, samples, sizeof(samples), 1) == 0);
    assert(special.code[0] == 0x20);
    hp4200_offset_result high_black = {0};
    fill(samples, 256);
    for (unsigned pass = 6; pass <= 10; ++pass)
        assert(hp4200_offset_observe(&high_black, pass, samples,
                                      sizeof(samples), 0) == 0);
    assert(high_black.black_level[0] == 256);
    assert(high_black.shading_black[0] == 0x3f);
    assert(high_black.final_reference[0] == 23 &&
           high_black.final_reference[2] == 25);
    puts("HP offset search tests passed");
    return 0;
}
