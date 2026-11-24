#include "hp4200_engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    size_t raw_lines;
    unsigned starts, finishes, bulk_calls;
    uint64_t now;
    int cancel;
    int two_line;
    int status_noise;
    size_t pause_at;
    size_t powerdrop_at;
    int marker_enabled;
    size_t marker_at;
    int stream_mode;
    size_t stream_line_bytes, stream_bytes;
    unsigned ten_ms_sleeps;
    size_t observed_raw_lines, observer_fail_at;
} fake;

static int read_reg(void *context, uint8_t reg, uint8_t *value)
{
    (void)context;
    assert(reg == 1 || reg == 0x58);
    *value = reg == 1 ? 80 : 0;
    return 0;
}

static int write_reg(void *context, uint8_t reg, uint8_t value)
{
    fake *f = context;
    if (reg == 7 && value == 3) ++f->starts;
    if (reg == 7 && value == 2) ++f->finishes;
    return 0;
}

static int bulk_read(void *context, uint8_t *data, size_t length)
{
    fake *f = context;
    if (f->stream_mode) {
        ++f->bulk_calls;
        for (size_t i = 0; i < length; ++i) {
            size_t position = f->stream_bytes + i;
            size_t column = position % f->stream_line_bytes;
            data[i] = column < f->stream_line_bytes - 2 ?
                      (uint8_t)((position / f->stream_line_bytes) % 251) : 0;
        }
        f->stream_bytes += length;
        return 0;
    }
    size_t line_bytes = f->two_line ? 32 : 31;
    assert(length % line_bytes == 0);
    ++f->bulk_calls;
    for (size_t row = 0; row < length / line_bytes; ++row) {
        uint8_t *line = data + row * line_bytes;
        unsigned shift = f->two_line ? 1 : 0;
        for (unsigned pixel = 0; pixel < 10; ++pixel) {
            line[shift + pixel * 3] = (uint8_t)(f->raw_lines + pixel);
            line[shift + pixel * 3 + 1] =
                (uint8_t)(f->raw_lines + pixel + 40);
            line[shift + pixel * 3 + 2] =
                (uint8_t)(f->raw_lines + pixel + 80);
        }
        if (shift)
            line[0] = f->status_noise && f->raw_lines >= 4 ? 0x30 :
                      f->raw_lines < 3 ? 0 : 3;
        line[line_bytes - 1] = f->raw_lines == f->pause_at ? 0x10 :
                                shift ? 3 : 0;
        if (f->raw_lines == f->powerdrop_at)
            line[line_bytes - 1] |= 0x20;
        if (f->marker_enabled)
            line[line_bytes - 4] =
                f->raw_lines == f->marker_at ? 0 : 0xff;
        ++f->raw_lines;
    }
    return 0;
}

static uint64_t tick(void *context) { return ++((fake *)context)->now; }
static void sleep_ms(void *context, unsigned ms)
{
    if (ms == 10) ++((fake *)context)->ten_ms_sleeps;
}
static int cancelled(void *context) { return ((fake *)context)->cancel; }

static int observe_raw(void *context, size_t row,
                       const uint8_t *data, size_t length)
{
    fake *f = context;
    assert(row == f->observed_raw_lines && length == 31);
    assert(data[0] == (uint8_t)(row + 3));
    ++f->observed_raw_lines;
    return row == f->observer_fail_at ? -1 : 0;
}

int main(void)
{
    fake f = {.pause_at = SIZE_MAX, .powerdrop_at = SIZE_MAX,
              .observer_fail_at = SIZE_MAX};
    hp4200_io io = {.context = &f, .read_register = read_reg,
                    .write_register = write_reg, .bulk_read = bulk_read,
                    .monotonic_ms = tick, .sleep_ms = sleep_ms,
                    .cancelled = cancelled, .raw_observer_context = &f,
                    .observe_raw_line = observe_raw};
    hp4200_scan_plan plan;
    hp4200_register_patch patch;
    hp4200_register_bank bank;
    hp4200_engine engine;
    uint8_t output[21];
    assert(hp4200_plan_rgb(75, 75, 30, 750, &plan) == 0);
    assert(plan.raw_pixels == 10 && plan.output_pixels == 7);
    hp4200_timing_patch(&plan, &patch);
    assert(hp4200_crop_patch(&patch, 0, 120, 30, 64, 64) == 0);
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    bank.value[0x25]++;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == -1);
    bank.value[0x25]--;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == 0);
    assert(f.finishes == 0);
    for (unsigned row = 0; row < 3; ++row) {
        assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
        if (row == 0) assert(f.finishes == 1);
        assert(output[0] == 3 + row * 2);
        assert(output[1] == 44 + row * 2);
        assert(output[2] == 85 + row * 2);
    }
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 0);
    assert(engine.output_seen == 3 && f.starts == 1 && f.finishes == 1);
    assert(f.bulk_calls == 1);
    assert(engine.pause_requests == 0);
    assert(engine.powerdrop_events == 0);
    assert(f.observed_raw_lines == engine.raw_seen &&
           !engine.raw_capture_error);
    assert(hp4200_engine_end(&engine) == 0);
    assert(f.finishes == 1);
    io.observe_raw_line = NULL;
    /* Force multiple raw-buffer refills. HP returns the motor only after
     * the last planned byte has been read, then drains buffered RGB rows. */
    f.raw_lines = 0;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 120, NULL) == 0);
    engine.stream_capacity = plan.transport_line_bytes * 3;
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
    assert(f.finishes == 1);
    unsigned rows = 1;
    while (hp4200_engine_next_row(&engine, output, sizeof(output)) == 1)
        ++rows;
    assert(rows == engine.output_rows && f.finishes == 2);
    assert(hp4200_engine_end(&engine) == 0 && f.finishes == 2);
    /* When ReadScan returns the last output row before the final raw refill,
     * HP StopScan performs the return instead. Do not fetch an extra line. */
    f.raw_lines = 0;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 120, NULL) == 0);
    engine.stream_capacity = plan.transport_line_bytes * 2;
    rows = 0;
    while (hp4200_engine_next_row(&engine, output, sizeof(output)) == 1)
        ++rows;
    assert(rows == engine.output_rows);
    assert(engine.stream_remaining == plan.transport_line_bytes);
    assert(f.finishes == 2 && engine.motor_running);
    assert(hp4200_engine_end(&engine) == 0 && f.finishes == 3);
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == 0);
    f.cancel = 1;
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == -2);
    assert(f.finishes == 4 && !engine.motor_running);
    f.cancel = 0;
    f.raw_lines = 0;
    f.two_line = 1;
    f.status_noise = 1;
    plan.two_line_mode = 1;
    plan.transport_line_bytes = 32;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == 0);
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
    assert(output[0] == 3 && output[1] == 44 && output[2] == 85);
    assert(engine.two_line_shift == 1);
    assert(engine.pause_requests == 0);
    assert(engine.powerdrop_events == 0);
    assert(hp4200_engine_end(&engine) == 0);
    f.raw_lines = 0;
    f.status_noise = 0;
    f.pause_at = 4;
    f.powerdrop_at = 4;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == 0);
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
    assert(engine.pause_requests == 1 && engine.pause_rows[0] == 1);
    assert(engine.powerdrop_events == 1 && engine.powerdrop_rows[0] == 1);
    assert(hp4200_engine_end(&engine) == 0);
    f.pause_at = SIZE_MAX;
    f.powerdrop_at = SIZE_MAX;
    f.two_line = 0;
    f.raw_lines = 0;
    f.marker_enabled = 1;
    f.marker_at = 4;
    plan.two_line_mode = 0;
    plan.transport_line_bytes = 31;
    hp4200_near_top top = {.marker_search_rows = 3,
                            .top_rows = 1, .marker_extra_rows = 2};
    assert(hp4200_engine_begin_near_top(&engine, &io, &plan, &bank,
            12, NULL, &top) == 0);
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
    assert(engine.top_marker_found && engine.top_ready);
    assert(output[0] == 8 && output[1] == 49 && output[2] == 90);
    assert(hp4200_engine_end(&engine) == 0);
    f.raw_lines = 0;
    f.marker_at = SIZE_MAX;
    assert(hp4200_engine_begin_near_top(&engine, &io, &plan, &bank,
            12, NULL, &top) == 0);
    assert(hp4200_engine_next_row(&engine, output, sizeof(output)) == 1);
    assert(!engine.top_marker_found && engine.top_ready);
    assert(output[0] == 7 && output[1] == 48 && output[2] == 89);
    assert(hp4200_engine_end(&engine) == 0);
    /* A full-width 600 dpi line exceeds 15 KiB. The HP USB primitive
     * splits at 65,535 bytes, often halfway through one of these lines. */
    assert(hp4200_plan_rgb(600, 600, 2550, 750, &plan) == 0);
    assert(plan.raw_pixels == 5100 && plan.output_pixels == 5100);
    hp4200_timing_patch(&plan, &patch);
    assert(hp4200_crop_patch(&patch, 0, 120, 2550, 64, 64) == 0);
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    f.stream_mode = 1;
    f.stream_bytes = 0;
    f.stream_line_bytes = plan.transport_line_bytes;
    uint8_t *wide_output = malloc(plan.output_pixels * 3);
    assert(wide_output);
    unsigned prior_finishes = f.finishes;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 5, NULL) == 0);
    assert(hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 1);
    assert(f.bulk_calls > 1 && f.finishes == prior_finishes + 1);
    assert(wide_output[0] == 12 && wide_output[1] == 16 &&
           wide_output[2] == 20);
    assert(wide_output[3] == 12 && wide_output[4] == 16 &&
           wide_output[5] == 20);
    rows = 1;
    while (hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 1) ++rows;
    assert(rows == 10 && engine.output_seen == 10);
    assert(hp4200_engine_end(&engine) == 0 &&
           f.finishes == prior_finishes + 1);
    /* The optional larger host buffer keeps this 15 MiB strip in one
     * read while the default retains HP's 8 MiB refill boundary. */
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 500, NULL) == 0);
    assert(engine.stream_remaining > 0x800000 &&
           engine.stream_remaining < 0x1000000);
    assert(engine.stream_capacity ==
           (0x800000 / plan.transport_line_bytes) *
           plan.transport_line_bytes);
    assert(hp4200_engine_end(&engine) == 0);
    io.host_buffer_limit = 0x1000000;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 500, NULL) == 0);
    assert(engine.stream_capacity == engine.stream_remaining);
    assert(hp4200_engine_end(&engine) == 0);
    io.host_buffer_limit = 0x8000000;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 500, NULL) == 0);
    assert(engine.stream_capacity == engine.stream_remaining);
    assert(hp4200_engine_end(&engine) == 0);
    io.host_buffer_limit = 0;
    /* HP reduces the host allocation and holds back a short final raw
     * segment when the selected area reaches the bottom of the glass. */
    plan.near_bed_end = 1;
    f.stream_bytes = 0;
    prior_finishes = f.finishes;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 5, NULL) == 0);
    size_t expected_reserve = ((0x17800 - plan.transport_line_bytes) /
                               plan.transport_line_bytes) *
                              plan.transport_line_bytes;
    size_t expected_capacity =
        ((engine.stream_remaining / plan.transport_line_bytes * 3) >> 2) *
        plan.transport_line_bytes;
    assert(engine.stream_reserve == expected_reserve &&
           engine.stream_capacity == expected_capacity);
    assert(hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 1);
    assert(f.stream_bytes == engine.stream_capacity &&
           f.finishes == prior_finishes);
    rows = 1;
    while (hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 1) ++rows;
    assert(rows == 10);
    assert(f.ten_ms_sleeps == 1);
    assert(hp4200_engine_end(&engine) == 0 &&
           f.finishes == prior_finishes + 1);
    /* HP accepts 600 dpi sensor samples with 1200 dpi motor travel.
     * Both axes must keep their own output dimensions. */
    assert(hp4200_plan_rgb(600, 1200, 30, 750, &plan) == 0);
    assert(plan.raw_pixels == 60 && plan.output_pixels == 60 &&
           plan.motor_y_dpi == 1200);
    hp4200_timing_patch(&plan, &patch);
    assert(hp4200_crop_patch(&patch, 0, 120, 30, 64, 64) == 0);
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    f.stream_bytes = 0;
    f.stream_line_bytes = plan.transport_line_bytes;
    prior_finishes = f.finishes;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 5, NULL) == 0);
    rows = 0;
    while (hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 1) ++rows;
    assert(rows == 20 && engine.output_seen == 20);
    assert(hp4200_engine_end(&engine) == 0 &&
           f.finishes == prior_finishes + 1);
    /* A full-width 600 x 1200 job crosses two ordinary 8 MiB host-buffer
     * boundaries. Verify that the color-staggered output remains continuous
     * through both refills and the final motor return. */
    assert(hp4200_plan_rgb(600, 1200, 2550, 750, &plan) == 0);
    hp4200_timing_patch(&plan, &patch);
    assert(hp4200_crop_patch(&patch, 0, 120, 2550, 64, 64) == 0);
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    f.stream_bytes = 0;
    f.stream_line_bytes = plan.transport_line_bytes;
    prior_finishes = f.finishes;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 400, NULL) == 0);
    assert(engine.output_rows == 1600 &&
           engine.stream_remaining > engine.stream_capacity * 2);
    for (unsigned row = 0; row < 1600; ++row) {
        assert(hp4200_engine_next_row(&engine, wide_output,
                                      plan.output_pixels * 3) == 1);
        assert(wide_output[0] == (uint8_t)((24 + row) % 251));
        assert(wide_output[1] == (uint8_t)((32 + row) % 251));
        assert(wide_output[2] == (uint8_t)((40 + row) % 251));
    }
    assert(hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 0);
    assert(engine.output_seen == 1600 && !engine.motor_running &&
           f.finishes == prior_finishes + 1);
    assert(hp4200_engine_end(&engine) == 0);
    /* Exercise the complete 600 x 1200 bed height without allocating an
     * image. This covers the row counters and final motor return after many
     * standard-buffer refills, including the near-bed-end plan flag. */
    plan.near_bed_end = 1;
    f.stream_bytes = 0;
    prior_finishes = f.finishes;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank,
                               HP4200_MAX_Y_300, NULL) == 0);
    assert(engine.output_rows == (size_t)HP4200_MAX_Y_300 * 4 &&
           engine.stream_remaining > engine.stream_capacity * 25 &&
           engine.stream_reserve == 0);
    for (size_t row = 0; row < engine.output_rows; ++row) {
        assert(hp4200_engine_next_row(&engine, wide_output,
                                      plan.output_pixels * 3) == 1);
        assert(wide_output[0] == (uint8_t)((24 + row) % 251));
        assert(wide_output[1] == (uint8_t)((32 + row) % 251));
        assert(wide_output[2] == (uint8_t)((40 + row) % 251));
    }
    assert(hp4200_engine_next_row(&engine, wide_output,
                                  plan.output_pixels * 3) == 0);
    assert(!engine.motor_running && f.finishes == prior_finishes + 1 &&
           f.stream_bytes == (engine.leading_rows + engine.raw_rows +
                              engine.stagger_rows) *
                             plan.transport_line_bytes);
    assert(hp4200_engine_end(&engine) == 0);
    /* A diagnostic sink failure must not turn a valid scanner image into
     * a failed scan, and it must not be called again after its first error. */
    f.stream_mode = 0;
    f.raw_lines = 0;
    f.observed_raw_lines = 0;
    f.observer_fail_at = 1;
    assert(hp4200_plan_rgb(75, 75, 30, 750, &plan) == 0);
    hp4200_timing_patch(&plan, &patch);
    assert(hp4200_crop_patch(&patch, 0, 120, 30, 64, 64) == 0);
    hp4200_register_bank_init(&bank);
    hp4200_register_bank_apply(&bank, &patch);
    io.observe_raw_line = observe_raw;
    assert(hp4200_engine_begin(&engine, &io, &plan, &bank, 12, NULL) == 0);
    while (hp4200_engine_next_row(&engine, output, sizeof(output)) == 1) { }
    assert(engine.output_seen == 3 && engine.raw_capture_error &&
           f.observed_raw_lines == 2);
    assert(hp4200_engine_end(&engine) == 0);
    free(wide_output);
    puts("HP ordinary RGB engine tests passed");
    return 0;
}
