#include "hp4200_origin_scan.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STRIDE = 901, HEIGHT = 150 };
typedef struct {
    uint8_t image[STRIDE * HEIGHT];
    uint8_t write_address[16], write_value[16];
    unsigned writes, sensor_reads;
    size_t image_read;
    uint64_t now;
    int cancel;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    if (reg == 0x58) *value = 0x34;
    else if (reg == 2) *value = f->sensor_reads++ == 0 ? 2 : 0;
    else if (reg == 1) *value = 255;
    else return -1;
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->writes < 16);
    f->write_address[f->writes] = reg;
    f->write_value[f->writes++] = value;
    return 0;
}

static int bulk_read(void *context, uint8_t *data, size_t length)
{
    fake *f = context;
    assert(f->image_read + length <= sizeof(f->image));
    memcpy(data, f->image + f->image_read, length);
    f->image_read += length;
    return 0;
}

static uint64_t clock_ms(void *context) { return ((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms) { ((fake *)context)->now += ms; }
static int cancelled(void *context) { return ((fake *)context)->cancel; }

int main(void)
{
    fake *f = calloc(1, sizeof(*f));
    hp4200_register_bank bank;
    uint16_t x = 0, y = 0;
    assert(f);
    hp4200_io io = {.context = f, .read_register = read_reg,
                    .write_register = write_reg, .bulk_read = bulk_read,
                    .monotonic_ms = clock_ms, .sleep_ms = sleep_ms,
                    .cancelled = cancelled};
    hp4200_register_bank_init(&bank);
    assert((((unsigned)bank.value[0x24] << 8) | bank.value[0x25]) -
           ((((unsigned)bank.value[0x22] << 8) | bank.value[0x23])) == 600);
    memset(f->image, 80, sizeof(f->image));
    f->image[21 * STRIDE + 0x97] = 0;
    for (unsigned col = 0; col < 80; ++col)
        f->image[26 * STRIDE + 1 + col * 3] = 0;
    assert(hp4200_measure_origin(&io, &bank, 0x3e, &x, &y) == 0);
    assert(x == 0x40 + 20 && y == 41);
    assert(f->image_read == sizeof(f->image));
    assert(f->write_address[f->writes - 1] == 7 &&
           f->write_value[f->writes - 1] == 2);
    assert(f->now == 200);
    free(f);
    puts("HP origin acquisition sequence tests passed");
    return 0;
}
