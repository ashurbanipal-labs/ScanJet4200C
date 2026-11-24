#include "hp4200_calibration_setup.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    uint8_t address[40], value[40];
    unsigned writes;
} fake;

static int write_reg(void *context, uint8_t address, uint8_t value)
{
    fake *f = context;
    assert(f->writes < 40);
    f->address[f->writes] = address;
    f->value[f->writes++] = value;
    return 0;
}

static int find(const fake *f, uint8_t address)
{
    for (unsigned i = 0; i < f->writes; ++i)
        if (f->address[i] == address) return f->value[i];
    return -1;
}

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .write_register = write_reg};
    hp4200_register_bank bank;
    hp4200_register_bank_init(&bank);
    assert(hp4200_write_color_calibration_setup(&io, &bank, 0x1234, 600) == 0);
    assert(f.writes == 37);
    assert(f.address[0] == 8 && f.address[36] == 0x5a);
    assert(find(&f, 9) == (0x1a | 0x38));
    assert(find(&f, 0x22) == 0x12 && find(&f, 0x23) == 0x34);
    assert(find(&f, 0x24) == 0x26 && find(&f, 0x25) == 0x20);
    assert(find(&f, 0x3b) == 1 && find(&f, 0x3c) == 1);
    assert(find(&f, 0x4e) == 31 && find(&f, 0x4f) == 2);
    assert(find(&f, 0x50) == 0 && find(&f, 0x5a) == 0);
    assert(hp4200_write_color_calibration_setup(&io, &bank, 0xffff, 600) == -1);
    assert(f.writes == 37);
    {
        const uint8_t gain[] = {33, 23, 33};
        f.writes = 0;
        assert(hp4200_write_offset_setup(&io, &bank, gain) == 0);
        assert(f.writes == 17);
        assert(f.address[0] == 9 && f.address[16] == 0x4e);
        assert(find(&f, 9) == 0x38 && find(&f, 0x0a) == 0);
        assert(find(&f, 0x23) == 0x27 && find(&f, 0x24) == 4);
        assert(find(&f, 0x25) == 0xd7 && find(&f, 0x4e) == 9);
        assert(find(&f, 0x3b) == 33 && find(&f, 0x3c) == 23);
        f.writes = 0;
        const uint8_t offsets[] = {38, 2, 37};
        assert(hp4200_write_shading_setup(&io, &bank, 122,
                                           gain, offsets) == 0);
        assert(f.writes == 37);
        assert(find(&f, 0x23) == 122);
        assert(find(&f, 0x24) == 0x14 && find(&f, 0x25) == 0x66);
        assert(find(&f, 0x47) == 0x52 && find(&f, 0x4e) == 30);
        assert(find(&f, 0x38) == 38 && find(&f, 0x3d) == 33);
    }
    puts("HP color calibration register setup tests passed");
    return 0;
}
