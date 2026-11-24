#include "hp4200_engine.h"
#include "hp4200_motor.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* FUN_10004f30 grows HP's initial 1 MiB host limit to one eighth of RAM,
 * capped at 8 MiB. Optional larger caps test whether fewer host-buffer
 * refills save time. FUN_10001260 then applies the near-end reserve and
 * rounds the allocation to lines. */
static size_t stream_capacity(size_t total, size_t line_bytes,
                              int near_bed_end, size_t host_limit,
                              size_t *reserve)
{
    size_t capacity = total < host_limit ? total : host_limit;
    *reserve = 0;
    if (near_bed_end && total / 5 * 4 + total % 5 * 4 / 5 <= capacity) {
        size_t lines = ((capacity / line_bytes) * 3) >> 2;
        if (!lines) lines = 1;
        *reserve = ((0x17800 - line_bytes) / line_bytes) * line_bytes;
        capacity = lines * line_bytes;
        if (capacity < *reserve) capacity = *reserve;
    }
    return capacity / line_bytes * line_bytes;
}

static int next_raw_line(hp4200_engine *engine)
{
    size_t line_bytes = engine->plan.transport_line_bytes;
    if (engine->stream_offset == engine->stream_used) {
        size_t count = engine->stream_remaining < engine->stream_capacity ?
                       engine->stream_remaining : engine->stream_capacity;
        /* FUN_10001fd0 leaves a 0x17800-byte, line-aligned tail near the
         * end of a scan that reaches the last 50 bed units. */
        if (engine->stream_reserve &&
            engine->stream_remaining <
                engine->stream_reserve + engine->stream_capacity) {
            size_t before_reserve = engine->stream_remaining >
                engine->stream_reserve ?
                engine->stream_remaining - engine->stream_reserve :
                engine->stream_reserve;
            if (count > before_reserve) count = before_reserve;
            if (engine->stream_remaining <= engine->stream_reserve)
                engine->io->sleep_ms(engine->io->context, 10);
        }
        /* The marker can end the prelude early. Read one raw line at a time
         * until HP's near-top search and its variable skip are resolved. */
        if (engine->near_top && !engine->top_ready && count > line_bytes)
            count = line_bytes;
        if (!count) return -1;
        uint64_t start_ms = engine->io->monotonic_ms(engine->io->context);
        int status = hp4200_read_buffer(engine->io, engine->stream_buffer,
                                         count);
        uint64_t elapsed_ms = engine->io->monotonic_ms(engine->io->context) -
                              start_ms;
        engine->read_ms += elapsed_ms;
        if (elapsed_ms > engine->max_read_ms)
            engine->max_read_ms = elapsed_ms;
        if (status) return status;
        engine->stream_used = count;
        engine->stream_offset = 0;
        engine->stream_remaining -= count;
        /* HP FUN_10001fd0 calls FUN_10001a70 as soon as its planned
         * raw-byte count reaches zero, before it finishes converting the
         * buffered rows for the caller. Do not wait for session_end: the
         * motor must return while the host drains this final buffer. */
        if (!engine->stream_remaining && engine->motor_running) {
            engine->motor_running = 0;
            if (hp4200_motor_finish(engine->io)) return -1;
        }
    }
    if (engine->stream_used - engine->stream_offset < line_bytes) return -1;
    memcpy(engine->raw_line, engine->stream_buffer + engine->stream_offset,
           line_bytes);
    engine->stream_offset += line_bytes;
    return 0;
}

static int engine_begin(hp4200_engine *engine, const hp4200_io *io,
                         const hp4200_scan_plan *plan,
                         const hp4200_register_bank *bank,
                         unsigned height_300dpi_units,
                         const hp4200_color_transform *color,
                         const hp4200_near_top *top)
{
    unsigned separation;
    unsigned scan_start, scan_end;
    if (!engine || !io || !plan || !bank || !height_300dpi_units ||
        (io->host_buffer_limit &&
         io->host_buffer_limit != 0x800000 &&
         io->host_buffer_limit != 0x1000000 &&
         io->host_buffer_limit != 0x8000000) ||
        height_300dpi_units > HP4200_MAX_Y_300 ||
        !plan->raw_pixels || !plan->output_pixels ||
        plan->output_pixels > plan->raw_pixels ||
        plan->transport_line_bytes !=
            plan->raw_pixels * 3 + 1 + plan->two_line_mode ||
        plan->two_line_mode > 1)
        return -1;
    scan_start = ((unsigned)bank->value[0x22] << 8) | bank->value[0x23];
    scan_end = ((unsigned)bank->value[0x24] << 8) | bank->value[0x25];
    if (scan_end <= scan_start ||
        scan_end - scan_start != plan->width_300dpi_units * 2 ||
        (scan_end - scan_start) * plan->sensor_x_dpi / 600 !=
        plan->raw_pixels) return -1;
    memset(engine, 0, sizeof(*engine));
    engine->io = io;
    engine->plan = *plan;
    engine->near_top = top != NULL;
    engine->top_ready = top == NULL ||
        (top->marker_search_rows == 0 && top->top_rows == 0);
    if (top) {
        engine->top_search_remaining = top->marker_search_rows;
        engine->top_skip_remaining = top->top_rows;
        engine->top_extra_rows = top->marker_extra_rows;
    }
    engine->two_line_first_row = plan->two_line_mode;
    engine->raw_rows = (size_t)height_300dpi_units * plan->motor_y_dpi / 300;
    engine->output_rows =
        (size_t)height_300dpi_units * plan->requested_y_dpi / 300;
    engine->leading_rows = plan->motor_y_dpi * 12 / 600;
    separation = plan->motor_y_dpi * 4 / 600;
    engine->stagger_rows = separation * 2;
    if (!engine->raw_rows || !engine->output_rows ||
        engine->output_rows > engine->raw_rows || !separation) return -1;
    size_t total_rows = engine->leading_rows + engine->raw_rows +
                        engine->stagger_rows;
    if (top && !engine->top_ready)
        total_rows += top->marker_search_rows + top->top_rows +
                      top->marker_extra_rows;
    if (total_rows > SIZE_MAX / plan->transport_line_bytes) return -1;
    engine->stream_remaining = total_rows * plan->transport_line_bytes;
    engine->stream_capacity = stream_capacity(engine->stream_remaining,
                                               plan->transport_line_bytes,
                                               plan->near_bed_end,
                                               io->host_buffer_limit ?
                                               io->host_buffer_limit : 0x800000,
                                               &engine->stream_reserve);
    engine->raw_line = malloc(plan->transport_line_bytes);
    engine->stream_buffer = malloc(engine->stream_capacity);
    engine->vertical_mask = malloc(engine->raw_rows);
    if (!engine->raw_line || !engine->stream_buffer ||
        !engine->vertical_mask ||
        hp4200_sample_mask(engine->vertical_mask, engine->raw_rows,
                            engine->output_rows) != engine->output_rows ||
        hp4200_pipeline_init(&engine->pipeline, plan->raw_pixels,
                              plan->output_pixels, separation, color)) {
        hp4200_engine_end(engine);
        return HP4200_ENGINE_NO_MEMORY;
    }
    if (hp4200_write_scan_table(io, bank) || hp4200_motor_start(io)) {
        hp4200_engine_end(engine);
        return HP4200_ENGINE_START_FAILED;
    }
    engine->motor_running = 1;
    engine->active = 1;
    return 0;
}

int hp4200_engine_begin(hp4200_engine *engine, const hp4200_io *io,
                         const hp4200_scan_plan *plan,
                         const hp4200_register_bank *bank,
                         unsigned height_300dpi_units,
                         const hp4200_color_transform *color)
{
    return engine_begin(engine, io, plan, bank, height_300dpi_units,
                        color, NULL);
}

int hp4200_engine_begin_near_top(hp4200_engine *engine,
                                  const hp4200_io *io,
                                  const hp4200_scan_plan *plan,
                                  const hp4200_register_bank *bank,
                                  unsigned height_300dpi_units,
                                  const hp4200_color_transform *color,
                                  const hp4200_near_top *top)
{
    if (!top) return -1;
    return engine_begin(engine, io, plan, bank, height_300dpi_units,
                        color, top);
}

int hp4200_engine_next_row(hp4200_engine *engine, uint8_t *output,
                            size_t output_capacity)
{
    if (!engine || !engine->active || !output ||
        output_capacity < engine->plan.output_pixels * 3) return -1;
    if (engine->output_seen >= engine->output_rows) return 0;
    while (engine->raw_seen < engine->raw_rows + engine->stagger_rows) {
        int read_status = next_raw_line(engine);
        if (read_status) {
            hp4200_engine_end(engine);
            return read_status;
        }
        if (engine->prelude_seen < engine->leading_rows) {
            ++engine->prelude_seen;
            continue;
        }
        if (engine->near_top && !engine->top_ready) {
            if (engine->top_search_remaining) {
                uint8_t marker = engine->raw_line[
                    engine->plan.transport_line_bytes - 4];
                --engine->top_search_remaining;
                if (marker < 0x32) {
                    engine->top_marker_found = 1;
                    engine->top_search_remaining = 0;
                    engine->top_skip_remaining += engine->top_extra_rows;
                }
                if (engine->top_search_remaining ||
                    engine->top_skip_remaining) continue;
            } else if (engine->top_skip_remaining) {
                --engine->top_skip_remaining;
                if (engine->top_skip_remaining) continue;
            }
            engine->top_ready = 1;
            engine->stream_remaining =
                (engine->raw_rows + engine->stagger_rows) *
                engine->plan.transport_line_bytes;
            continue;
        }
        size_t scan_row = engine->raw_seen++;
        if (engine->io->observe_raw_line && !engine->raw_capture_error &&
            engine->io->observe_raw_line(engine->io->raw_observer_context,
                                          scan_row, engine->raw_line,
                                          engine->plan.transport_line_bytes))
            engine->raw_capture_error = 1;
        size_t shift = 0;
        if (engine->plan.two_line_mode) {
            size_t last = engine->plan.transport_line_bytes - 1;
            /* HP FUN_10001ae0 checks this marker on the first row. Once
             * chosen, the one-byte offset applies to every following row. */
            if (engine->two_line_first_row) {
                engine->two_line_first_row = 0;
                if (engine->raw_line[0] == engine->raw_line[last] &&
                    engine->raw_line[last - 1] != engine->raw_line[last])
                    engine->two_line_shift = 1;
            }
            shift = (size_t)engine->two_line_shift;
        }
        /* The extra byte in HP's two-line framing is outside the selected
         * image row. Its bits are not a second line-status field. */
        size_t status_at = shift + engine->plan.raw_pixels * 3;
        uint8_t status = engine->raw_line[status_at];
        if (status & 0x10) {
            if (engine->pause_requests < HP4200_PAUSE_HISTORY)
                engine->pause_rows[engine->pause_requests] = scan_row;
            ++engine->pause_requests;
        }
        /* LM9830 register-02 trailer bit 5 records a supply dip below 3 V.
         * It can be cleared by a register read, so record each raw line's
         * trailer before the pixel pipeline discards it. */
        if (status & 0x20) {
            if (engine->powerdrop_events < HP4200_PAUSE_HISTORY)
                engine->powerdrop_rows[engine->powerdrop_events] = scan_row;
            ++engine->powerdrop_events;
        }
        int ready = hp4200_pipeline_push(&engine->pipeline,
                                          engine->raw_line + shift,
                                          engine->plan.raw_pixels * 3,
                                          output, output_capacity);
        if (ready < 0) {
            hp4200_engine_end(engine);
            return -1;
        }
        if (ready) {
            size_t output_row = scan_row - engine->stagger_rows;
            if (output_row < engine->raw_rows &&
                engine->vertical_mask[output_row]) {
                ++engine->output_seen;
                return 1;
            }
        }
    }
    hp4200_engine_end(engine);
    return -1;
}

int hp4200_engine_end(hp4200_engine *engine)
{
    int status = 0;
    if (!engine) return -1;
    if (engine->motor_running) {
        status = hp4200_motor_finish(engine->io);
        engine->motor_running = 0;
    }
    hp4200_pipeline_destroy(&engine->pipeline);
    free(engine->raw_line);
    free(engine->stream_buffer);
    free(engine->vertical_mask);
    engine->raw_line = NULL;
    engine->stream_buffer = NULL;
    engine->vertical_mask = NULL;
    engine->active = 0;
    return status;
}
