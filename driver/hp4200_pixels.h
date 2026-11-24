#ifndef HP4200_PIXELS_H
#define HP4200_PIXELS_H

#include <stddef.h>
#include <stdint.h>

/* FUN_10001340: build the DLL's evenly distributed sample-selection mask.
 * Valid for downsampling or a 1:1 copy; the minidriver's higher-resolution
 * output behavior has not yet been established. Returns selected count. */
size_t hp4200_sample_mask(uint8_t *mask, size_t raw_pixels,
                          size_t output_pixels);

/* RGB branch of FUN_10001ae0, after removal of the one/two status bytes.
 * Compacts selected RGB samples in place. Returns output bytes. */
size_t hp4200_compact_rgb(uint8_t *rgb, size_t raw_pixels,
                          const uint8_t *mask);

typedef struct {
    int32_t coefficient[3][3];
    uint8_t table[3][1024];
} hp4200_color_transform;

/* Matrix constants selected by FUN_100035c0. Lookup tables are calculated
 * later from calibration, so this initializes coefficients only. */
void hp4200_color_matrix(hp4200_color_transform *transform,
                          int special_sensor_mode);

/* FUN_10002d60: three-channel matrix, clamp to 10 bits, then LUT. */
void hp4200_transform_rgb(uint8_t *rgb, size_t pixels,
                          const hp4200_color_transform *transform);

#endif
