#ifndef HP4200_SESSION_H
#define HP4200_SESSION_H

#include "hp4200_engine.h"
#include "hp4200_offset.h"
#include "hp4200_final_capture.h"
#include "hp4200_ram.h"

typedef struct {
    unsigned x_300, y_300, width_300, height_300;
    unsigned x_dpi, y_dpi;
} hp4200_scan_request;

typedef struct {
    hp4200_io io;
    hp4200_scan_plan plan;
    hp4200_register_bank registers;
    hp4200_color_transform color;
    hp4200_engine engine;
    hp4200_offset_result offset;
    hp4200_shading_result shading;
    hp4200_ram_diagnostic ram_diagnostic;
    hp4200_ram_diagnostic final_ram_diagnostic;
    uint16_t origin_x, origin_y;
    uint8_t gain[3];
    unsigned brightness, measured_kib_per_s, sensor_bits, ram_retries;
    unsigned final_plane_rewrites;
    int lamp_on, started, ended, stage, detail;
} hp4200_session;

/* The caller owns the transport and keeps its context alive until end.
 * Coordinates are in 300-dpi units. The complete HP RGB calibration and
 * motor setup run here; no SANE state is used. Returns zero on success. */
int hp4200_session_begin(hp4200_session *session, const hp4200_io *io,
                          const hp4200_scan_request *request);


/* Returns one RGB row, zero at image end, or a negative error. */
int hp4200_session_next_row(hp4200_session *session, uint8_t *output,
                             size_t capacity);

/* Stop/return the motor, extinguish the lamp and release session buffers.
 * Safe after partial initialization and safe to call more than once. */
int hp4200_session_end(hp4200_session *session);


#endif
