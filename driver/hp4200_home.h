#ifndef HP4200_HOME_H
#define HP4200_HOME_H

#include "hp4200_io.h"

/* hpad32.dll FUN_10002740: request motion toward home if the status bit is
 * not already set. FUN_10002800 waits for and clears the home-sensor state. */
int hp4200_home_start(const hp4200_io *io);
int hp4200_home_wait_clear(const hp4200_io *io);

/* LampOn/LampOff's four-register hardware pattern. */
int hp4200_lamp_on(const hp4200_io *io);
int hp4200_lamp_off(const hp4200_io *io);

#endif
