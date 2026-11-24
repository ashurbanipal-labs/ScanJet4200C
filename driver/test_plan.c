#include "hp4200_plan.h"
#include "hp4200_registers.h"
#include <assert.h>
#include <stdio.h>

typedef struct { unsigned writes; uint8_t first_reg, last_reg; } fake_bus;

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake_bus *bus = context;
    (void)value;
    if (bus->writes == 0) bus->first_reg = reg;
    bus->last_reg = reg;
    ++bus->writes;
    return 0;
}

int main(void)
{
    hp4200_scan_plan p;
    hp4200_register_patch patch;
    assert(hp4200_plan_rgb(600, 600, 2480, 750, &p) == 0);
    assert(p.raw_pixels == 4960 && p.output_pixels == 4960);
    assert(p.reg08 == 6 && p.motor_step == 5409);
    assert(p.phase_difference == 5409 && p.two_line_mode == 1);
    assert(p.line_bytes == 14881 && p.transport_line_bytes == 14882);
    assert(p.pause_kib == 80 && p.fast_feed_step == 225);
    assert(hp4200_factory_registers[0x20] == 0x15);
    assert(hp4200_factory_registers[0x21] == 0x18);
    assert(hp4200_factory_registers[0x5e] == 0x80);
    assert((hp4200_factory_registers[0x5e] & 0x20) == 0);
    hp4200_timing_patch(&p, &patch);
    assert(patch.valid[0x08] && patch.value[0x08] == 0x06);
    assert(patch.value[0x44] == 0xff && patch.value[0x5a] == 2);
    assert(patch.value[0x46] == 0x15 && patch.value[0x47] == 0x21);
    assert(patch.value[0x52] == 0x15 && patch.value[0x53] == 0x21);
    assert(!patch.valid[0x20]);
    assert(hp4200_crop_patch(&patch, 0, 40, 2480, 0x40, 0x3e) == 0);
    assert(patch.value[0x22] == 0 && patch.value[0x23] == 0x40);
    assert(patch.value[0x24] == 0x13 && patch.value[0x25] == 0xa0);
    assert(patch.value[0x4a] == 0 && patch.value[0x4b] == 0x66);
    assert(hp4200_crop_patch(&patch, 0, 0, 2480, 0x40, 0x3e) == -1);
    hp4200_near_top top;
    assert(hp4200_crop_patch_near_top(&patch, 0, 0, 2480,
            0x40, 0x3e, 600, &top) == 0);
    assert(top.marker_search_rows == 120 && top.top_rows == 0 &&
           top.marker_extra_rows == 48);
    assert(patch.value[0x4a] == 0 && patch.value[0x4b] == 12);
    assert(hp4200_crop_patch_near_top(&patch, 0, 39, 2480,
            0x40, 0x3e, 600, &top) == 0);
    assert(top.top_rows == 78 && patch.value[0x4b] == 12);
    assert(hp4200_crop_patch_near_top(&patch, 0, 40, 2480,
            0x40, 0x3e, 600, &top) == -1);
    hp4200_register_bank bank;
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    assert(bank.value[0x08] == 6 && bank.value[0x20] == 0x15);
    fake_bus bus = {0};
    hp4200_io io = {.context = &bus, .write_register = write_reg};
    assert(hp4200_write_scan_table(&io, &bank) == 0);
    assert(bus.writes == 37 && bus.first_reg == 0x08 && bus.last_reg == 0x5a);
    assert(hp4200_plan_rgb(75, 75, 2480, 750, &p) == 0);
    assert(p.raw_pixels == 826 && p.output_pixels == 620);
    assert(p.line_rate_factor == 2 && p.motor_y_dpi == 150);
    assert(hp4200_plan_rgb(1200, 600, 2480, 750, &p) == -1);
    puts("HP planner tests passed");
    return 0;
}
