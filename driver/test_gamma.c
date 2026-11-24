#include "hp4200_gamma.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned write_count;
    uint8_t reg[9], value[9];
    unsigned port;
    size_t bytes;
    uint8_t stored[1024];
    unsigned reads;
} fake;

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->write_count < 9);
    f->reg[f->write_count] = reg;
    f->value[f->write_count++] = value;
    return 0;
}

static int bulk_write(void *context, uint8_t port,
                      const uint8_t *data, size_t length)
{
    fake *f = context;
    assert(data[0] == 0x42 && data[1023] == 0x99);
    f->port = port;
    f->bytes = length;
    memcpy(f->stored, data, length);
    return 0;
}

static int bulk_read(void *context, uint8_t port,
                     uint8_t *data, size_t length)
{
    fake *f = context;
    assert(port == 6 && length == sizeof(f->stored));
    assert(f->value[f->write_count - 2] == 0x20);
    memcpy(data, f->stored, length);
    ++f->reads;
    return 0;
}

int main(void)
{
    fake f = {0};
    hp4200_io io = {.context = &f, .write_register = write_reg,
                    .bulk_write_ram = bulk_write, .bulk_read_ram = bulk_read};
    uint8_t table[1024] = {[0] = 0x42, [1023] = 0x99};
    assert(hp4200_upload_color_table(&io, 2, table) == 0);
    assert(f.write_count == 9 && f.reg[0] == 3 && f.value[0] == 4);
    assert(f.reg[1] == 4 && f.value[1] == 0);
    assert(f.reg[2] == 5 && f.value[2] == 0);
    assert(f.reg[4] == 4 && f.value[4] == 0x20);
    assert(f.reg[7] == 4 && f.value[7] == 0);
    assert(f.port == 6 && f.bytes == sizeof(table) && f.reads == 1);
    uint8_t curve[256], expanded[1024];
    memset(curve, 0, sizeof(curve));
    curve[1] = 100;
    curve[255] = 240;
    hp4200_expand_256_to_1024(curve, expanded);
    assert(expanded[0] == 0 && expanded[1] == 25);
    assert(expanded[2] == 50 && expanded[3] == 75);
    assert(expanded[4] == 100 && expanded[1023] == 240);
    assert(hp4200_build_asic_table(expanded, 0, 10, 0) == 0);
    assert(expanded[0] == 0 && expanded[1023] == 255);
    assert(hp4200_build_asic_table(expanded, 2, 10, 1) == 0);
    assert(expanded[0] == 0 && expanded[1023] == 255);
    assert(hp4200_build_asic_table(expanded, 3, 10, 0) == -1);
    puts("HP color table upload tests passed");
    return 0;
}
