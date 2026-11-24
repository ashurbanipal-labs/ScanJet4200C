#ifndef HP4200_BRIGHTNESS_H
#define HP4200_BRIGHTNESS_H

#include "hp4200_registers.h"

/* HP FUN_10006680: scan one calibration line and report its mean byte value.
 * Preconditions: initialized ASIC and lit lamp. Does not toggle sensor mode. */
int hp4200_probe_brightness(const hp4200_io *io,
                             const hp4200_register_bank *factory,
                             unsigned *mean_brightness);

/* FUN_10006680 returns a retry request below 130, toggles sensor mode when
 * the mean is zero, and fails a value below 20 after three seconds. The
 * caller counts prior zero-triggered toggles. */
enum {
    HP4200_BRIGHTNESS_ACCEPT = 0,
    HP4200_BRIGHTNESS_RETRY = 1,
    HP4200_BRIGHTNESS_TOGGLE = 2,
    HP4200_BRIGHTNESS_FAIL = -1
};
int hp4200_brightness_decide(unsigned mean, unsigned zero_toggles,
                              uint64_t elapsed_ms);

#endif
