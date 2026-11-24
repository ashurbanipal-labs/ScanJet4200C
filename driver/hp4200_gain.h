#ifndef HP4200_GAIN_H
#define HP4200_GAIN_H

#include "hp4200_io.h"

/* Offline-portable translation of hpad32.dll FUN_10006ff0 and FUN_10006e90.
 * sample_bytes is the HP test-line length excluding its 2-byte trailer.
 * The caller must have prepared the HP calibration register image first. */
int hp4200_calibrate_gain(const hp4200_io *io, size_t sample_bytes,
                          int color_mode, unsigned mono_channel,
                          uint8_t gain[3]);

#endif
