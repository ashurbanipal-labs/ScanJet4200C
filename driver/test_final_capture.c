#include "hp4200_final_capture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { LINE_BYTES = 30602 };
typedef struct {
    uint8_t reg[256];
    size_t raw_read;
    unsigned writes, ram_reads;
    int sensor16;
    uint64_t now;
    uint8_t stored[3][10316];
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    if (reg != 1) return -1;
    *value = 255;
    (void)f;
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    ((fake *)context)->reg[reg] = value;
    return 0;
}

static int read_bulk(void *context, uint8_t *data, size_t length)
{
    fake *f = context;
    for (size_t i = 0; i < length; ++i) {
        size_t position = (f->raw_read + i) % LINE_BYTES;
        data[i] = position >= LINE_BYTES - 2 ? 0 :
                  position % 2 == 0 ? 0x09 : 0x60;
    }
    f->raw_read += length;
    return 0;
}

static int write_bulk(void *context, uint8_t port,
                      const uint8_t *data, size_t length)
{
    fake *f = context;
    assert(port == 6 && length == 10316);
    assert(data[0] == 0 &&
           data[1] == (f->sensor16 ? (f->writes + 1) * 40 : 0));
    assert(f->reg[3] == ((f->writes << 1) | 1));
    memcpy(f->stored[f->writes], data, length);
    ++f->writes;
    return 0;
}

static int read_ram(void *context, uint8_t port,
                    uint8_t *data, size_t length)
{
    fake *f = context;
    unsigned channel = f->reg[3] >> 1;
    assert(port == 6 && length == 10316 && channel < 3 &&
           f->reg[4] == 0x20);
    memcpy(data, f->stored[channel], length);
    ++f->ram_reads;
    return 0;
}

static uint64_t monotonic_ms(void *context) { return ((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms) { ((fake *)context)->now += ms; }

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .bulk_read = read_bulk,
                    .bulk_write_ram = write_bulk, .bulk_read_ram = read_ram,
                    .monotonic_ms = monotonic_ms, .sleep_ms = sleep_ms};
    hp4200_register_bank bank;
    hp4200_shading_result result = {0};
    const uint8_t gain[] = {33, 23, 33}, offset[] = {38, 2, 37};
    hp4200_register_bank_init(&bank);
    assert(hp4200_calibrate_shading_rgb(&io, &bank, 64 + 361,
                                         gain, offset, &result) == -1);
    assert(f.raw_read == 0 && f.writes == 0);
    assert(hp4200_calibrate_shading_rgb(&io, &bank, 122,
                                         gain, offset, &result) == 0);
    assert(f.raw_read == 9 * LINE_BYTES && f.writes == 3 && f.ram_reads == 3);
    assert(result.reg3e == 9 && result.minimum_white == 600);
    assert(result.maximum_white == 600 && result.plane_bytes == 10316);
    f = (fake){0};
    f.sensor16 = 1;
    const uint16_t black[] = {10, 20, 30};
    assert(hp4200_calibrate_shading_rgb_mode(&io, &bank, 122,
            gain, offset, black, 16, 1, &result) == 0);
    assert(result.reg3e == 0x19 && f.writes == 3 && f.ram_reads == 3);
    puts("HP full RGB shading calibration sequence tests passed");
    return 0;
}
