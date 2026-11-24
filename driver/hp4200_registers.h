#ifndef HP4200_REGISTERS_H
#define HP4200_REGISTERS_H

#include "hp4200_plan.h"
#include "hp4200_io.h"
#include <stdint.h>

/* GetScannerAbility in hpad32.dll: 0x09f6 × 0x0db6 at 300 dpi. */
#define HP4200_MAX_X_300 2550U
#define HP4200_MAX_Y_300 3510U

/* Initial 0x60-byte LM9830 image copied by hpad32.dll FUN_100019a0.
 * The HP driver updates only selected entries before sending setup tables. */
extern const uint8_t hp4200_factory_registers[0x60];

typedef struct {
    uint8_t value[0x60];
    uint8_t valid[0x60];
} hp4200_register_patch;

/* Only the planner-derived timing/mode registers. It is NOT a complete
 * scanner initialization or a safe stand-alone scan command. */
void hp4200_timing_patch(const hp4200_scan_plan *plan,
                          hp4200_register_patch *patch);

/* Ordinary crop branch of FUN_100035c0, for top coordinate >=40 in the
 * DLL's 300-dpi units. Uses the origin found during HP home calibration. */
int hp4200_crop_patch(hp4200_register_patch *patch,
                       unsigned x_300, unsigned y_300,
                       unsigned width_300, unsigned home_x,
                       unsigned home_y);

typedef struct {
    unsigned marker_search_rows;
    unsigned top_rows;
    unsigned marker_extra_rows;
} hp4200_near_top;

/* FUN_100035c0's y<40 branch. Returns the raw-line counts used by
 * FUN_10001fd0 to locate the glass-edge marker before image rows. */
int hp4200_crop_patch_near_top(hp4200_register_patch *patch,
                                unsigned x_300, unsigned y_300,
                                unsigned width_300, unsigned home_x,
                                unsigned home_y, unsigned motor_y_dpi,
                                hp4200_near_top *top);

typedef struct {
    uint8_t value[0x60];
} hp4200_register_bank;

void hp4200_register_bank_init(hp4200_register_bank *bank);
void hp4200_register_bank_apply(hp4200_register_bank *bank,
                                 const hp4200_register_patch *patch);
/* Exact ordered address list built by FUN_10006b10 and written by
 * FUN_10006320. The caller must have completed HP initialization and
 * calibration before invoking this on real hardware. */
int hp4200_write_scan_table(const hp4200_io *io,
                             const hp4200_register_bank *bank);

#endif
