#include "hp4200_home.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t registers[256];
    uint8_t writes_reg[16], writes_value[16];
    uint8_t sensor[8];
    unsigned writes, sensor_reads, sensor_count;
    uint64_t now;
    int cancel;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    if (reg == 2 && f->sensor_reads < f->sensor_count)
        *value = f->sensor[f->sensor_reads++];
    else
        *value = f->registers[reg];
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->writes < sizeof(f->writes_reg));
    f->writes_reg[f->writes] = reg;
    f->writes_value[f->writes++] = value;
    f->registers[reg] = value;
    return 0;
}

static uint64_t monotonic_ms(void *context) { return ((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms) { ((fake *)context)->now += ms; }
static int cancelled(void *context) { return ((fake *)context)->cancel; }

static hp4200_io make_io(fake *f)
{
    hp4200_io io = {.context = f, .read_register = read_reg,
                    .write_register = write_reg, .monotonic_ms = monotonic_ms,
                    .sleep_ms = sleep_ms, .cancelled = cancelled};
    return io;
}

int main(void)
{
    fake f = {0};
    hp4200_io io = make_io(&f);
    f.registers[0x58] = 0x34;
    f.sensor[0] = 0;
    f.sensor_count = 1;
    assert(hp4200_home_start(&io) == 0);
    assert(f.writes == 3 && f.now == 50);
    assert(f.writes_reg[0] == 0x58 && f.writes_value[0] == 0x24);
    assert(f.writes_reg[1] == 0x58 && f.writes_value[1] == 0x34);
    assert(f.writes_reg[2] == 7 && f.writes_value[2] == 2);

    memset(&f, 0, sizeof(f));
    f.registers[0x58] = 0x34;
    f.sensor[0] = 2;
    f.sensor_count = 1;
    assert(hp4200_home_start(&io) == 0);
    assert(f.writes == 2 && f.now == 0);

    memset(&f, 0, sizeof(f));
    f.cancel = 1;
    assert(hp4200_home_start(&io) == -2 && f.writes == 0);

    memset(&f, 0, sizeof(f));
    f.registers[0x58] = 0x34;
    f.sensor[0] = 0;
    f.sensor[1] = 2;
    f.sensor[2] = 2;
    f.sensor[3] = 0;
    f.sensor_count = 4;
    assert(hp4200_home_wait_clear(&io) == 0);
    assert(f.writes == 3 && f.now == 120);
    assert(f.writes_reg[0] == 0x58 && f.writes_value[0] == 0x24);
    assert(f.writes_reg[1] == 7 && f.writes_value[1] == 1);
    assert(f.writes_reg[2] == 0x58 && f.writes_value[2] == 0x34);

    memset(&f, 0, sizeof(f));
    f.registers[0x58] = 0x34;
    assert(hp4200_home_wait_clear(&io) == -3);
    assert(f.writes == 3 && f.writes_reg[2] == 7 && f.writes_value[2] == 0);
    assert(f.now == 15000);

    memset(&f, 0, sizeof(f));
    f.registers[0x58] = 0x34;
    f.cancel = 1;
    assert(hp4200_home_wait_clear(&io) == -2);
    assert(f.writes == 3 && f.writes_reg[2] == 7);

    memset(&f, 0, sizeof(f));
    assert(hp4200_lamp_on(&io) == 0);
    assert(f.writes == 4);
    assert(f.registers[0x2c] == 0x3f && f.registers[0x2d] == 0xff);
    assert(f.registers[0x2e] == 0 && f.registers[0x2f] == 1);
    f.registers[0x69] = 0xff;
    assert(hp4200_lamp_off(&io) == 0);
    assert(f.writes == 9 && f.registers[0x69] == 0xf7);
    assert(f.registers[0x2c] == 0 && f.registers[0x2d] == 1);
    assert(f.registers[0x2e] == 0x3f && f.registers[0x2f] == 0xff);
    puts("HP home and lamp sequence tests passed");
    return 0;
}
