#ifndef HP4200_RATE_H
#define HP4200_RATE_H

#include "hp4200_io.h"

/* HP FUN_10002930: three 64 KiB direct USB reads, with HP's 650..750
 * KiB/s clamp. This is distinct from image SRAM polling. */
int hp4200_measure_usb_rate(const hp4200_io *io, unsigned *kib_per_second);

#endif
