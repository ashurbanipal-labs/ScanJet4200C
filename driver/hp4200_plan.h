#ifndef HP4200_PLAN_H
#define HP4200_PLAN_H

/* 24-bit RGB branch of hpad32.dll FUN_100035c0. The resulting plan is
 * derived from the DLL's default register image; initialization, exact
 * cropping and calibration remain separate driver stages. */
typedef struct {
    unsigned requested_x_dpi, requested_y_dpi;
    unsigned width_300dpi_units;
    unsigned sensor_x_dpi, motor_y_dpi, line_rate_factor;
    unsigned raw_pixels, line_bytes, transport_line_bytes, output_pixels;
    unsigned reg08, motor_step, phase_difference, two_line_mode;
    unsigned pause_kib, fast_feed_step;
    unsigned near_bed_end; /* SetScanParameter: y + height > 3460 units. */
} hp4200_scan_plan;

int hp4200_plan_rgb(unsigned x_dpi, unsigned y_dpi,
                     unsigned width_300dpi_units,
                     unsigned measured_kib_per_s,
                     hp4200_scan_plan *out);

#endif
