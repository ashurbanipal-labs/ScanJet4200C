#ifndef HP4200_MOTOR_H
#define HP4200_MOTOR_H

#include "hp4200_io.h"

/* HP StartScan's final motor sequence (after register writes, calibration,
 * color tables, and host buffer setup). */
int hp4200_motor_start(const hp4200_io *io);

/* HP FUN_10001a70, called by StopScan: stop and return toward home. */
int hp4200_motor_finish(const hp4200_io *io);

#endif
