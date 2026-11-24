#include "hp4200_initialize.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t registers[256], color[3][1024], shading[3][0x2aa8];
    unsigned bridge_writes, sensor_reads, ram_writes, ram_reads;
    uint64_t now;
    int home_requested, home_cleared;
    int corrupt_first, corrupt_always;
} fake;

static int write_bridge(void *context, uint8_t index, uint8_t value)
{
    static const uint8_t sequence[] = {2, 0x99, 0x66, 0xcc, 0x33, 0x16};
    fake *f = context;
    unsigned phase = f->bridge_writes % 6;
    assert(index == (phase == 0 || phase == 5 ? 3 : 0));
    assert(value == sequence[phase]);
    ++f->bridge_writes;
    return 0;
}

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    fake *f = context;
    if (reg == 2) {
        *value = f->sensor_reads++ % 3 == 1 ? 2 : 0;
    } else if (reg == 0x69) {
        *value = 0x10;
    } else {
        *value = f->registers[reg];
    }
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    assert(f->bridge_writes >= 6);
    if (reg == 7 && value == 2) f->home_requested = 1;
    if (reg == 7 && value == 1) {
        assert(f->home_requested);
        f->home_cleared = 1;
    }
    f->registers[reg] = value;
    return 0;
}

static int write_ram(void *context, uint8_t port,
                     const uint8_t *data, size_t length)
{
    fake *f = context;
    unsigned channel = f->registers[3] >> 1;
    assert(f->home_cleared && port == 6 && channel < 3);
    assert(f->registers[4] == 0);
    assert(length == (f->registers[3] & 1 ? 0x2aa8U : 1024U));
    memcpy(f->registers[3] & 1 ? f->shading[channel] : f->color[channel],
           data, length);
    ++f->ram_writes;
    return 0;
}

static int read_ram(void *context, uint8_t port, uint8_t *data, size_t length)
{
    fake *f = context;
    unsigned channel = f->registers[3] >> 1;
    assert(port == 6 && channel < 3 && f->registers[4] == 0x20);
    assert(length == (f->registers[3] & 1 ? 0x2aa8U : 1024U));
    memcpy(data, f->registers[3] & 1 ? f->shading[channel] : f->color[channel],
           length);
    if ((f->registers[3] & 1) && channel == 1 &&
        (f->corrupt_always || (f->corrupt_first && f->bridge_writes == 6)))
        data[3] ^= 1;
    ++f->ram_reads;
    return 0;
}

static uint64_t monotonic_ms(void *context) { return ((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms) { ((fake *)context)->now += ms; }

int main(void)
{
    fake f = {0};
    f.registers[0x58] = 0x34;
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .write_bridge = write_bridge,
                    .bulk_read_ram = read_ram, .bulk_write_ram = write_ram,
                    .monotonic_ms = monotonic_ms, .sleep_ms = sleep_ms};
    assert(hp4200_initialize_core(&io) == 0);
    assert(f.bridge_writes == 6);
    assert(f.home_requested && f.home_cleared);
    assert(f.ram_writes == 6 && f.ram_reads == 6);
    memset(&f, 0, sizeof(f));
    f.registers[0x58] = 0x34;
    assert(hp4200_initialize_core_mode(&io, 8) == 0);
    assert(f.registers[0x0c] == 0x60);
    assert(f.ram_writes == 6 && f.ram_reads == 6);
    memset(&f, 0, sizeof(f));
    f.corrupt_first = 1;
    hp4200_ram_diagnostic diagnostic = {0};
    unsigned retries = 0;
    assert(hp4200_initialize_core_mode_with_ram_retry(&io, 16,
                &diagnostic, &retries) == 0);
    assert(retries == 1 && !diagnostic.has_mismatch);
    assert(f.bridge_writes == 12 && f.ram_writes == 13);
    assert(f.now >= 500);
    memset(&f, 0, sizeof(f));
    f.corrupt_always = 1;
    assert(hp4200_initialize_core_mode_with_ram_retry(&io, 16,
                &diagnostic, &retries) == -1151);
    assert(retries == 1 && diagnostic.has_mismatch);
    assert(diagnostic.shading == 1 && diagnostic.channel == 1);
    assert(f.bridge_writes == 12);
    puts("HP core initialization sequence tests passed");
    return 0;
}
