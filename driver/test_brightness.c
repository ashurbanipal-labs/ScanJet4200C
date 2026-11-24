#include "hp4200_brightness.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t reg45;
    uint8_t wrote_reg[8], wrote_value[8];
    unsigned writes, reads;
    uint64_t now;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    if (reg == 0x45) *value = f->reg45;
    else if (reg == 1) *value = 255;
    else return -1;
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->writes < 8);
    f->wrote_reg[f->writes] = reg;
    f->wrote_value[f->writes++] = value;
    return 0;
}

static int bulk_read(void *context, uint8_t *data, size_t length)
{
    fake *f = context;
    assert(length <= 1801);
    memset(data, 150, length);
    data[length - 1] = 0;
    ++f->reads;
    return 0;
}

static uint64_t monotonic_ms(void *context) { return ((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms) { ((fake *)context)->now += ms; }

int main(void)
{
    fake f = {.reg45 = 0x3f};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .bulk_read = bulk_read,
                    .monotonic_ms = monotonic_ms, .sleep_ms = sleep_ms};
    hp4200_register_bank bank;
    unsigned mean = 0;
    hp4200_register_bank_init(&bank);
    assert(hp4200_probe_brightness(&io, &bank, &mean) == 0);
    assert(mean == 150 && f.reads == 1 && f.now == 50);
    assert(f.writes == 6);
    assert(f.wrote_reg[0] == 0x45 && f.wrote_value[0] == 0x2f);
    assert(f.wrote_reg[5] == 0x45 && f.wrote_value[5] == 0x3f);
    assert(hp4200_brightness_decide(0, 0, 1000) == HP4200_BRIGHTNESS_TOGGLE);
    assert(hp4200_brightness_decide(0, 1, 4000) == HP4200_BRIGHTNESS_TOGGLE);
    assert(hp4200_brightness_decide(0, 2, 4000) == HP4200_BRIGHTNESS_FAIL);
    assert(hp4200_brightness_decide(19, 0, 3000) == HP4200_BRIGHTNESS_RETRY);
    assert(hp4200_brightness_decide(19, 0, 3001) == HP4200_BRIGHTNESS_FAIL);
    assert(hp4200_brightness_decide(20, 0, 4000) == HP4200_BRIGHTNESS_RETRY);
    assert(hp4200_brightness_decide(129, 0, 4000) == HP4200_BRIGHTNESS_RETRY);
    assert(hp4200_brightness_decide(130, 0, 4000) == HP4200_BRIGHTNESS_ACCEPT);
    puts("HP lamp brightness probe tests passed");
    return 0;
}
