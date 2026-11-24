#include "hp4200_registers.h"
#include <string.h>

/* hpad32.dll image 0x1000e044 through 0x1000e0a3, verified against the
 * extracted PrecisionScan LT 2.03 DLL's .data section. Register 0x5e=0x80
 * sets the LM9830 CDS TEST-pin output (bit 7); forced ADC input is bit 5
 * and remains clear. */
const uint8_t hp4200_factory_registers[0x60] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1a, 0x00, 0x0a, 0x6c, 0x2f, 0x13, 0x06,
    0x17, 0x01, 0x03, 0x03, 0x05, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x21, 0x00, 0x40,
    0x15, 0x18, 0x00, 0x40, 0x02, 0x98, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x3f, 0xff, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x30, 0x25, 0x25, 0x24, 0x28, 0x24, 0x28, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x1d, 0x00, 0x13, 0x05, 0x48, 0x01, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x5e, 0x02,
    0x00, 0x15, 0x00, 0x45, 0x00, 0x10, 0x08, 0x17, 0x2b, 0x90, 0x00, 0x00, 0x01, 0x00, 0x80, 0x00,
};

static void set(hp4200_register_patch *p, unsigned reg, unsigned value)
{
    p->valid[reg] = 1;
    p->value[reg] = (uint8_t)value;
}

void hp4200_timing_patch(const hp4200_scan_plan *plan,
                          hp4200_register_patch *patch)
{
    unsigned selector;
    if (!plan || !patch) return;
    memset(patch, 0, sizeof(*patch));
    if (plan->sensor_x_dpi <= 100) selector = 3;
    else if (plan->sensor_x_dpi <= 150) selector = 2;
    else if (plan->sensor_x_dpi <= 200) selector = 1;
    else if (plan->sensor_x_dpi <= 300) selector = 2;
    else if (plan->sensor_x_dpi <= 400) selector = 1;
    else selector = 0;
    set(patch, 0x08, plan->reg08);
    set(patch, 0x09, selector | 0x18); /* ordinary 24-bit color */
    set(patch, 0x0a, plan->line_rate_factor == 2 ? 1 : 0);
    set(patch, 0x44, plan->two_line_mode ? 0xff : 0);
    set(patch, 0x46, plan->motor_step >> 8);
    set(patch, 0x47, plan->motor_step);
    set(patch, 0x48, plan->fast_feed_step >> 8);
    set(patch, 0x49, plan->fast_feed_step);
    set(patch, 0x4e, plan->pause_kib);
    set(patch, 0x4f, 2);
    set(patch, 0x50, 0x3f);
    set(patch, 0x52, plan->phase_difference >> 8);
    set(patch, 0x53, plan->phase_difference);
    set(patch, 0x5a, plan->two_line_mode ? 2 : 0);
}

int hp4200_crop_patch(hp4200_register_patch *patch,
                       unsigned x_300, unsigned y_300,
                       unsigned width_300, unsigned home_x,
                       unsigned home_y)
{
    unsigned start, end, motor_start;
    if (!patch || !width_300 || x_300 > HP4200_MAX_X_300 || y_300 < 40 ||
        y_300 > HP4200_MAX_Y_300 ||
        width_300 > HP4200_MAX_X_300 - x_300 || home_x > 65535 ||
        home_y > 65535) return -1;
    start = home_x + 2 * x_300;
    end = start + 2 * width_300;
    motor_start = home_y + y_300;
    if (end > 65535 || motor_start > 65535) return -1;
    set(patch, 0x1e, home_x >> 8);
    set(patch, 0x1f, home_x);
    set(patch, 0x22, start >> 8);
    set(patch, 0x23, start);
    set(patch, 0x24, end >> 8);
    set(patch, 0x25, end);
    set(patch, 0x4a, motor_start >> 8);
    set(patch, 0x4b, motor_start);
    return 0;
}

int hp4200_crop_patch_near_top(hp4200_register_patch *patch,
                                unsigned x_300, unsigned y_300,
                                unsigned width_300, unsigned home_x,
                                unsigned home_y, unsigned motor_y_dpi,
                                hp4200_near_top *top)
{
    unsigned marker_300, start, end, motor_start;
    if (!patch || !top || y_300 >= 40 || !width_300 ||
        x_300 > HP4200_MAX_X_300 ||
        width_300 > HP4200_MAX_X_300 - x_300 ||
        home_x > 65535 || home_y > 65535 ||
        (motor_y_dpi != 150 && motor_y_dpi != 300 &&
         motor_y_dpi != 450 && motor_y_dpi != 600 &&
         motor_y_dpi != 1200)) return -1;
    marker_300 = home_y < 60 ? home_y : 60;
    top->marker_search_rows = marker_300 * motor_y_dpi / 300;
    top->top_rows = y_300 * motor_y_dpi / 300;
    top->marker_extra_rows = 24 * motor_y_dpi / 300;
    start = home_x + 2 * x_300;
    end = start + 2 * width_300;
    motor_start = home_y + y_300 + 10 -
        (top->marker_search_rows + top->top_rows) * 300 / motor_y_dpi;
    if (end > 65535 || motor_start > 65535) return -1;
    set(patch, 0x1e, home_x >> 8);
    set(patch, 0x1f, home_x);
    set(patch, 0x22, start >> 8);
    set(patch, 0x23, start);
    set(patch, 0x24, end >> 8);
    set(patch, 0x25, end);
    set(patch, 0x4a, motor_start >> 8);
    set(patch, 0x4b, motor_start);
    return 0;
}

void hp4200_register_bank_init(hp4200_register_bank *bank)
{
    if (bank) memcpy(bank->value, hp4200_factory_registers,
                     sizeof(bank->value));
}

void hp4200_register_bank_apply(hp4200_register_bank *bank,
                                 const hp4200_register_patch *patch)
{
    if (!bank || !patch) return;
    for (unsigned reg = 0; reg < 0x60; ++reg)
        if (patch->valid[reg]) bank->value[reg] = patch->value[reg];
}

int hp4200_write_scan_table(const hp4200_io *io,
                             const hp4200_register_bank *bank)
{
    static const uint8_t addresses[] = {
        0x08, 0x09, 0x0a, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x17,
        0x1e, 0x1f, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x39, 0x3a,
        0x3b, 0x3c, 0x3d, 0x3e, 0x44, 0x46, 0x47, 0x48, 0x49, 0x4a,
        0x4b, 0x4e, 0x4f, 0x50, 0x52, 0x53, 0x5a,
    };
    if (!io || !io->write_register || !bank) return -1;
    for (unsigned i = 0; i < sizeof(addresses); ++i)
        if (io->write_register(io->context, addresses[i],
                               bank->value[addresses[i]])) return -1;
    return 0;
}
