#include "hp4200_initialize.h"
#include "hp4200_registers.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t reg[0x60]; unsigned writes, reads; } fake;

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(reg < 0x60);
    f->reg[reg] = value;
    ++f->writes;
    return 0;
}

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    assert(reg < 0x60);
    *value = f->reg[reg];
    ++f->reads;
    return 0;
}

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg};
    assert(hp4200_initialize_register_image(&io) == 0);
    assert(f.writes == 88 && f.reads == 87);
    assert(f.reg[0x20] == 0x15 && f.reg[0x21] == 0x18);
    unsigned prior_writes = f.writes;
    assert(hp4200_probe_core_registers(&io) == 0);
    assert(f.writes == prior_writes + 3 && f.reads == 90);
    assert(memcmp(f.reg, hp4200_factory_registers + 0, 0x28) == 0);
    puts("HP register initialization tests passed");
    return 0;
}
