#ifndef HP4200_FINAL_CALIBRATION_H
#define HP4200_FINAL_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>
#include "hp4200_io.h"

/* FUN_100083c0 + FUN_100082f0: read nine big-endian sensor samples at each
 * horizontal position, convert to 10 bits, discard the four darkest, and
 * average the other five. The caller must provide nine complete lines. */
int hp4200_reduce_nine_lines(const uint8_t *raw, size_t stride_bytes,
                              size_t samples_per_line, uint16_t *output);

/* FUN_10007be0, RGB 8-bit path: convert reduced interleaved white samples
 * to three big-endian ASIC shading planes. plane_bytes must each hold
 * 2 * (left_pad + samples_per_channel) bytes. reg3e receives HP's scale mode. */
int hp4200_make_rgb_shading(const uint16_t *white, size_t samples_per_channel,
                            size_t left_pad, uint8_t *plane_bytes[3],
                            uint8_t *reg3e);

/* Same HP RGB path in the 16-bit sensor mode: subtract the offset pass's
 * capped black/4 value, limit coefficients to 0x3d4, set 0x3e bit 4, and
 * embed that same value in SRAM. */
int hp4200_make_rgb_shading_mode(const uint16_t *white,
                                 size_t samples_per_channel, size_t left_pad,
                                 uint8_t *plane_bytes[3], uint8_t *reg3e,
                                 const uint16_t shading_black[3],
                                 unsigned sensor_bits,
                                 unsigned sample_group);

/* FUN_10005f80: send the three planes to the LM9830 shading SRAM. */
int hp4200_upload_rgb_shading(const hp4200_io *io,
                              uint8_t *const plane_bytes[3], size_t length);

#endif
