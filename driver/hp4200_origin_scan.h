#ifndef HP4200_ORIGIN_SCAN_H
#define HP4200_ORIGIN_SCAN_H

#include "hp4200_registers.h"

/* HP FUN_10006960: acquire the 150-line reference image and locate home.
 * Preconditions: bridge and ASIC initialized, lamp ready, head sent home. */
int hp4200_measure_origin(const hp4200_io *io,
                          const hp4200_register_bank *factory,
                          uint16_t initial_y, uint16_t *origin_x,
                          uint16_t *origin_y);

#endif
