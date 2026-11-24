#ifndef HP4200_FINAL_CAPTURE_H
#define HP4200_FINAL_CAPTURE_H

#include "hp4200_registers.h"
#include "hp4200_offset.h"

typedef struct {
    uint8_t reg3e;
    unsigned minimum_white, maximum_white;
    size_t plane_bytes;
} hp4200_shading_result;

/* HP FUN_10008450 RGB 8-bit path: nine white lines, robust reduction,
 * correction-table generation, and upload to the three ASIC shading planes. */
int hp4200_calibrate_shading_rgb(const hp4200_io *io,
                                 const hp4200_register_bank *bank,
                                 unsigned origin_x,
                                 const uint8_t gain[3],
                                 const uint8_t offset[3],
                                 hp4200_shading_result *result);

int hp4200_calibrate_shading_rgb_mode(const hp4200_io *io,
                                      const hp4200_register_bank *bank,
                                      unsigned origin_x,
                                      const uint8_t gain[3],
                                      const uint8_t offset[3],
                                      const uint16_t shading_black[3],
                                      unsigned sensor_bits,
                                      unsigned sample_group,
                                      hp4200_shading_result *result);

#endif
