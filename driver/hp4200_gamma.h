#ifndef HP4200_GAMMA_H
#define HP4200_GAMMA_H

#include "hp4200_io.h"

/* FUN_10005e40: upload one already-calculated 1024-byte table into the
 * LM9830 image RAM. Table derivation is a separate calibration step. */
int hp4200_upload_color_table(const hp4200_io *io, unsigned channel,
                               const uint8_t table[1024]);

/* FUN_10002f50/FUN_10001910: derive the ASIC correction table from the
 * offset calibration's channel reference, then expand it to 1024 bytes.
 * The separate 1024-byte host lookup comes from expanding user_gamma. */
int hp4200_build_asic_table(uint8_t output[1024], unsigned channel,
                             unsigned black_reference, int special_sensor_mode);
void hp4200_expand_256_to_1024(const uint8_t input[256],
                                uint8_t output[1024]);

#endif
