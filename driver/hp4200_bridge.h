#ifndef HP4200_BRIDGE_H
#define HP4200_BRIDGE_H

#include "hp4200_io.h"

/* HP FUN_10005920 -> FUN_10005bf0 -> FUN_10005900. */
int hp4200_bridge_activate(const hp4200_io *io);

#endif
