#include "hp4200_final_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned channel, uploads, reads;
    uint8_t reg[6];
    uint8_t stored[3][6];
    uint8_t first_value[3];
} fake;

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(reg == 3 || reg == 4 || reg == 5);
    f->reg[reg] = value;
    return 0;
}

static int write_ram(void *context, uint8_t port, const uint8_t *data,
                     size_t length)
{
    fake *f = context;
    assert(port == 6 && length == 6 && f->reg[3] ==
           ((f->channel << 1) | 1) && f->reg[4] == 0);
    f->first_value[f->channel] = data[3];
    memcpy(f->stored[f->channel], data, length);
    ++f->channel;
    ++f->uploads;
    return 0;
}

static int read_ram(void *context, uint8_t port, uint8_t *data,
                    size_t length)
{
    fake *f = context;
    unsigned channel = f->reg[3] >> 1;
    assert(port == 6 && length == 6 && channel < 3 &&
           (f->reg[3] & 1) && f->reg[4] == 0x20);
    memcpy(data, f->stored[channel], length);
    ++f->reads;
    return 0;
}

int main(void)
{
    uint8_t lines[9][4];
    uint16_t output[2];
    const unsigned first[9] = {0, 20, 40, 60, 100, 100, 100, 100, 100};
    for (unsigned row = 0; row < 9; ++row) {
        unsigned value_a = first[row] << 2;
        unsigned value_b = (row + 1) * 10 << 2;
        lines[row][0] = (uint8_t)(value_a >> 8);
        lines[row][1] = (uint8_t)value_a;
        lines[row][2] = (uint8_t)(value_b >> 8);
        lines[row][3] = (uint8_t)value_b;
    }
    assert(hp4200_reduce_nine_lines(&lines[0][0], 4, 2, output) == 0);
    assert(output[0] == 100);
    assert(output[1] == 70); /* mean of 50, 60, 70, 80, 90 */
    {
        const uint16_t white[] = {500, 600, 1000, 750, 800, 900};
        uint8_t red[6], green[6], blue[6], mode;
        uint8_t *planes[] = {red, green, blue};
        assert(hp4200_make_rgb_shading(white, 2, 1, planes, &mode) == 0);
        assert(mode == 10);
        assert(red[0] == 0 && red[1] == 0);
        assert(((unsigned)red[2] << 8 | red[3]) == 535);
        assert(((unsigned)green[2] << 8 | green[3]) == 360);
        assert(((unsigned)blue[2] << 8 | blue[3]) == 11);
        assert(((unsigned)red[4] << 8 | red[5]) == 186);
        const uint16_t black[] = {10, 20, 30};
        assert(hp4200_make_rgb_shading_mode(white, 2, 1, planes, &mode,
                                             black, 16, 1) == 0);
        assert(mode == 0x1a);
        assert(((unsigned)red[0] << 8 | red[1]) == 40);
        assert(((unsigned)green[0] << 8 | green[1]) == 80);
        assert(((unsigned)blue[0] << 8 | blue[1]) == 120);
        assert(((unsigned)red[2] << 8 | red[3]) == 552);
        assert(((unsigned)green[2] << 8 | green[3]) == 369);
        assert(((unsigned)blue[2] << 8 | blue[3]) == 125);
        fake f = {0};
        hp4200_io io = {.context = &f, .write_register = write_reg,
                        .bulk_write_ram = write_ram,
                        .bulk_read_ram = read_ram};
        assert(hp4200_upload_rgb_shading(&io, planes, 6) == 0);
        assert(f.uploads == 3 && f.reads == 3 && f.reg[4] == 0 &&
               f.first_value[0] == red[3]);
        assert(f.first_value[1] == green[3] && f.first_value[2] == blue[3]);
    }
    {
        const uint16_t white[] = {500, 600, 700, 600, 700, 800,
                                  700, 800, 900, 800, 900, 1000};
        const uint16_t black[] = {10, 20, 30};
        uint8_t grouped[3][8], grouped_mode;
        uint8_t *grouped_planes[] = {grouped[0], grouped[1], grouped[2]};
        assert(hp4200_make_rgb_shading_mode(white, 4, 0, grouped_planes,
                                            &grouped_mode, black, 16, 2) == 0);
        assert(grouped_mode == 0x1a);
        for (unsigned channel = 0; channel < 3; ++channel)
            for (unsigned pair = 0; pair < 2; ++pair) {
                unsigned samples[2];
                for (unsigned index = 0; index < 2; ++index) {
                    unsigned sample = white[(pair * 2 + index) * 3 + channel] -
                                      black[channel];
                    if (sample > 0x3d4) sample = 0x3d4;
                    samples[index] = ((0x3d4 - sample) * 0x200) / sample;
                    if (samples[index] > 0x3d4) samples[index] = 0x3d4;
                }
                unsigned actual = ((unsigned)grouped[channel][pair * 2] << 8 |
                                   grouped[channel][pair * 2 + 1]);
                assert(actual == (((samples[0] + samples[1]) / 2) |
                                  ((unsigned)black[channel] << 2)));
            }
    }
    puts("HP final calibration math tests passed");
    return 0;
}
