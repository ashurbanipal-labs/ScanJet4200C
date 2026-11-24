#ifndef HP4200_OFFSET_H
#define HP4200_OFFSET_H

#include "hp4200_io.h"

typedef struct {
    uint8_t code[3];
    uint8_t positive_reference[3];
    uint32_t accumulated[3];
    uint16_t black_level[3];
    uint16_t shading_black[3];
    uint16_t final_reference[3];
    unsigned measurements;
} hp4200_offset_result;

/* FUN_100072e0's per-channel offset search and 5-pass black measurement.
 * The caller must first install the HP offset calibration register setup
 * (the part preceding FUN_100063c0). This routine does not initiate that
 * setup, so it must not yet be used as a stand-alone hardware scan. */
int hp4200_calibrate_offset(const hp4200_io *io, int special_sensor_mode,
                            hp4200_offset_result *result);

/* The pure observation step is exposed for deterministic verification of
 * the translated search, independent of scanner hardware. */
void hp4200_offset_prepare(hp4200_offset_result *result, unsigned pass);
int hp4200_offset_observe(hp4200_offset_result *result, unsigned pass,
                           const uint8_t *samples, size_t length,
                           int special_sensor_mode);

#endif
