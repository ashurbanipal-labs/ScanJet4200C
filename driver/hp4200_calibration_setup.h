#ifndef HP4200_CALIBRATION_SETUP_H
#define HP4200_CALIBRATION_SETUP_H

#include "hp4200_registers.h"

/* HP FUN_10006cb0: configure the 37-register color calibration table before
 * gain, offset, and shading calibration. The caller supplies the previously
 * measured horizontal home origin and the active scan parameter bank. */
int hp4200_write_color_calibration_setup(const hp4200_io *io,
                                         const hp4200_register_bank *bank,
                                         unsigned origin_x,
                                         unsigned horizontal_dpi);

/* FUN_100072e0 -> FUN_100063c0: flagged register transition from gain
 * calibration to the black-offset search. */
int hp4200_write_offset_setup(const hp4200_io *io,
                               const hp4200_register_bank *bank,
                               const uint8_t gain[3]);

/* FUN_10008450: full-width 600 dpi white-strip scan register table. */
int hp4200_write_shading_setup(const hp4200_io *io,
                                const hp4200_register_bank *bank,
                                unsigned origin_x, const uint8_t gain[3],
                                const uint8_t offset[3]);

#endif
