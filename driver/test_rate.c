#include "hp4200_rate.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned reads;
    uint64_t now;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    (void)context;
    assert(reg == 1);
    *value = 0x57;
    return 0;
}

static int bulk_read(void *context, uint8_t *data, size_t length)
{
    fake *f = context;
    assert(length == 65535 || length == 1);
    memset(data, 0, length);
    f->now += length == 1 ? 0 : 100;
    ++f->reads;
    return 0;
}

static uint64_t monotonic_ms(void *context) { return ((fake *)context)->now; }

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .bulk_read = bulk_read, .monotonic_ms = monotonic_ms};
    unsigned rate = 0;
    assert(hp4200_measure_usb_rate(&io, &rate) == 0);
    assert(f.reads == 6 && rate == 650); /* 635 -> HP's lower clamp */
    f.now = 0; f.reads = 0; rate = 700;
    assert(hp4200_measure_usb_rate(&io, &rate) == 0);
    assert(rate == 700); /* HP retains the previous faster measurement */
    puts("HP USB rate measurement tests passed");
    return 0;
}
