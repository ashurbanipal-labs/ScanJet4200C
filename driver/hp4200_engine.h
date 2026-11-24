#ifndef HP4200_ENGINE_H
#define HP4200_ENGINE_H

#include "hp4200_io.h"
#include "hp4200_pipeline.h"
#include "hp4200_plan.h"
#include "hp4200_registers.h"

enum { HP4200_PAUSE_HISTORY = 64 };
enum {
    HP4200_ENGINE_INVALID = -1,
    HP4200_ENGINE_NO_MEMORY = -2,
    HP4200_ENGINE_START_FAILED = -3
};

typedef struct {
    const hp4200_io *io;
    hp4200_scan_plan plan;
    hp4200_pipeline pipeline;
    uint8_t *raw_line;
    uint8_t *stream_buffer;
    uint8_t *vertical_mask;
    size_t stream_capacity, stream_used, stream_offset, stream_remaining;
    size_t stream_reserve;
    size_t raw_rows, output_rows, leading_rows, stagger_rows;
    size_t raw_seen, output_seen, prelude_seen;
    size_t top_search_remaining, top_skip_remaining, top_extra_rows;
    size_t pause_requests;
    size_t pause_rows[HP4200_PAUSE_HISTORY];
    size_t powerdrop_events;
    size_t powerdrop_rows[HP4200_PAUSE_HISTORY];
    int raw_capture_error;
    uint64_t read_ms, max_read_ms;
    int two_line_first_row, two_line_shift;
    int near_top, top_ready, top_marker_found;
    int motor_running, active;
} hp4200_engine;

/* Start the ordinary 24-bit RGB path after the caller has completed HP
 * scanner initialization, gain, offset, final calibration, and lookup table
 * setup. bank must contain those calibrated values. */
int hp4200_engine_begin(hp4200_engine *engine, const hp4200_io *io,
                         const hp4200_scan_plan *plan,
                         const hp4200_register_bank *bank,
                         unsigned height_300dpi_units,
                         const hp4200_color_transform *color);

int hp4200_engine_begin_near_top(hp4200_engine *engine,
                                  const hp4200_io *io,
                                  const hp4200_scan_plan *plan,
                                  const hp4200_register_bank *bank,
                                  unsigned height_300dpi_units,
                                  const hp4200_color_transform *color,
                                  const hp4200_near_top *top);

/* Returns 1 with an RGB row, 0 at end, or a negative error. */
int hp4200_engine_next_row(hp4200_engine *engine, uint8_t *output,
                            size_t output_capacity);

/* Stop and return the head, including after an incomplete scan. */
int hp4200_engine_end(hp4200_engine *engine);

#endif
