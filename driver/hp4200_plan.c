/* Readable C reconstruction of hpad32.dll's ordinary RGB scan plan.
 * Source: FUN_100035c0, FUN_100060c0, FUN_10002b50, FUN_10002af0,
 * FUN_10002930 in hpad32-decompiled.c.
 * This is not HP source code. It assumes the
 * DLL's default 5400-clock line and ordinary 24-bit color input. */
#include "hp4200_plan.h"
#include <stdint.h>

static unsigned clamp(unsigned value, unsigned lo, unsigned hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static unsigned sensor_x_dpi(unsigned requested)
{
    if (requested <= 100) return 100;
    if (requested <= 150) return 150;
    if (requested <= 200) return 200;
    if (requested <= 300) return 300;
    if (requested <= 400) return 400;
    return 600;
}

static unsigned motor_y_dpi(unsigned requested)
{
    if (requested <= 150) return 150;
    if (requested <= 300) return 300;
    if (requested <= 450) return 450;
    if (requested <= 600) return 600;
    return 1200;
}

/* With the DLL's initial register image, FUN_100060c0 reduces to this for
 * 24-bit RGB. The low-resolution color path uses two line-rate units. */
static void motor_timing(unsigned dpi, unsigned line_rate_factor,
                         unsigned *step, unsigned *tr, unsigned *dpd)
{
    unsigned pixels_per_line = line_rate_factor == 2 ? 5413 : 5409;
    unsigned reverse_and_ramp = 63 + 4 + 2;
    *tr = pixels_per_line;
    *step = pixels_per_line * dpi / 1200;
    *dpd = pixels_per_line - (reverse_and_ramp * *step * 4) % pixels_per_line;
}

int hp4200_plan_rgb(unsigned x_dpi, unsigned y_dpi,
                     unsigned width_300dpi_units,
                     unsigned measured_kib_per_s,
                     hp4200_scan_plan *out)
{
    hp4200_scan_plan p = {0};
    unsigned rate, rate_for_first_clock, code, denominator, tr;
    uint64_t scaled;

    if (!out || x_dpi == 0 || x_dpi > 600 || y_dpi == 0 || y_dpi > 1200 ||
        width_300dpi_units == 0 || width_300dpi_units > 2550 ||
        measured_kib_per_s == 0) return -1;

    p.requested_x_dpi = x_dpi;
    p.requested_y_dpi = y_dpi;
    p.width_300dpi_units = width_300dpi_units;
    p.sensor_x_dpi = sensor_x_dpi(x_dpi);
    p.motor_y_dpi = motor_y_dpi(y_dpi);
    p.line_rate_factor = p.sensor_x_dpi <= 200 ? 2 : 1;
    p.raw_pixels = width_300dpi_units * p.sensor_x_dpi / 300;
    p.output_pixels = width_300dpi_units * x_dpi / 300;
    p.line_bytes = p.raw_pixels * 3 + 1;
    rate = clamp(measured_kib_per_s, 650, 750);
    rate_for_first_clock = rate - (p.sensor_x_dpi >= 600 ? 50 : 0);

    motor_timing(p.motor_y_dpi, p.line_rate_factor,
                 &p.motor_step, &tr, &p.phase_difference);
    denominator = (p.motor_step * 3 * 2) / (p.line_rate_factor * 10);
    scaled = ((uint64_t)p.line_bytes * p.motor_y_dpi / rate_for_first_clock) * 2;
    code = clamp((unsigned)(scaled / denominator) + 1, 5, 65);

    if (code * tr / (p.line_rate_factor * 4) > 14000) {
        p.two_line_mode = 1;
        motor_timing(p.motor_y_dpi * 2, p.line_rate_factor,
                     &p.motor_step, &tr, &p.phase_difference);
        denominator = (p.motor_step * 3 * 2) / (p.line_rate_factor * 10);
        /* The motor DPI doubles above; the clock calculation still uses
         * the original motor_y_dpi. This was the earlier porting error. */
        scaled = ((uint64_t)(p.line_bytes + 1) * p.motor_y_dpi / rate) * 2;
        code = clamp((unsigned)(scaled / denominator) + 1, 5, 65);
    }
    p.reg08 = code - 2;
    /* The final two limits in FUN_10002b50 may raise the divider. */
    if (p.line_rate_factor * 1800 / p.motor_step > 1) {
        unsigned minimum = p.line_rate_factor * 1800 / p.motor_step - 1;
        if (p.reg08 < minimum) p.reg08 = minimum;
    }
    if (p.line_rate_factor * 7200 / tr > 1) {
        unsigned minimum = p.line_rate_factor * 7200 / tr - 1;
        if (p.reg08 < minimum) p.reg08 = minimum;
    }
    p.pause_kib = ((0x18000 - p.line_bytes) >> 10) - 1;
    /* FUN_100035c0 adds a second status byte in two-line motor mode. */
    p.transport_line_bytes = p.line_bytes + p.two_line_mode;
    p.fast_feed_step = (p.line_rate_factor * 225 * 8) / (p.reg08 + 2);
    *out = p;
    return 0;
}
