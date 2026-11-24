#include "hp4200_motor.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    uint8_t reg[16], value[16];
    unsigned writes, sleeps;
    int cancel;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    (void)context;
    assert(reg == 0x58);
    *value = 0x34;
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->writes < 16);
    f->reg[f->writes] = reg;
    f->value[f->writes++] = value;
    return 0;
}

static void sleep_ms(void *context, unsigned ms)
{
    fake *f = context;
    assert(ms == 50);
    ++f->sleeps;
}

static int cancelled(void *context) { return ((fake *)context)->cancel; }

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .sleep_ms = sleep_ms,
                    .cancelled = cancelled};
    assert(hp4200_motor_start(&io) == 0);
    assert(f.writes == 4 && f.sleeps == 1);
    assert(f.reg[0] == 0x58 && f.value[0] == 0x14);
    assert(f.reg[1] == 7 && f.value[1] == 8);
    assert(f.reg[2] == 7 && f.value[2] == 0);
    assert(f.reg[3] == 7 && f.value[3] == 3);
    assert(hp4200_motor_finish(&io) == 0);
    assert(f.writes == 7 && f.sleeps == 2);
    assert(f.reg[4] == 0x58 && f.value[4] == 0x34);
    assert(f.reg[5] == 7 && f.value[5] == 0);
    assert(f.reg[6] == 7 && f.value[6] == 2);
    f.cancel = 1;
    assert(hp4200_motor_start(&io) == -2);
    assert(f.writes == 7);
    puts("HP motor sequence tests passed");
    return 0;
}
