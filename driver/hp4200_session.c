#include "hp4200_session.h"

#include "hp4200_bridge.h"
#include "hp4200_brightness.h"
#include "hp4200_calibration_setup.h"
#include "hp4200_gain.h"
#include "hp4200_gamma.h"
#include "hp4200_home.h"
#include "hp4200_initialize.h"
#include "hp4200_origin_scan.h"
#include "hp4200_plan.h"
#include "hp4200_rate.h"
#include "hp4200_registers.h"

#include <string.h>

enum { HP4200_LAMP_WARMUP_LIMIT_MS = 30000 };

static int fail(hp4200_session *session, int stage, int detail)
{
    session->stage = stage;
    session->detail = detail;
    hp4200_session_end(session);
    return -1;
}

int hp4200_session_begin(hp4200_session *session, const hp4200_io *io,
                          const hp4200_scan_request *request)
{
    hp4200_register_patch patch;
    hp4200_near_top near_top;
    uint8_t table[1024];
    uint8_t identity[256];
    uint64_t lamp_started_ms = 0;
    unsigned zero_toggles = 0;
    int result = -1;
    if (!session || !io || !request || !io->read_register ||
        !io->write_register || !io->write_bridge || !io->bulk_read ||
        !io->bulk_read_ram || !io->bulk_write_ram ||
        !io->monotonic_ms || !io->sleep_ms) return -1;
    memset(session, 0, sizeof(*session));
    session->io = *io;
    session->io.verified_plane_rewrites = &session->final_plane_rewrites;
    session->io.final_ram_diagnostic = &session->final_ram_diagnostic;
    if (!request->width_300 || !request->height_300 ||
        !request->x_dpi || request->x_dpi > 600 ||
        !request->y_dpi || request->y_dpi > 1200 ||
        request->x_300 > HP4200_MAX_X_300 ||
        request->width_300 > HP4200_MAX_X_300 - request->x_300 ||
        request->y_300 > HP4200_MAX_Y_300 ||
        request->height_300 > HP4200_MAX_Y_300 - request->y_300 ||
        request->width_300 * request->x_dpi < 300 ||
        request->height_300 * request->y_dpi < 300) {
        /* No scan command has been sent yet; rejecting geometry must not
         * issue a stop command to the scanner. */
        session->stage = 1;
        session->detail = -1;
        session->ended = 1;
        return -1;
    }

    /* The DLL's shared-data byte DAT_1000f8fc starts at 0x10. A zero lamp
     * probe toggles 16/8-bit sensor mode, at most twice. A dim nonzero line
     * requests another probe. Keep every downstream color branch on the
     * selected mode. */
    session->sensor_bits = 16;
    for (unsigned mode_attempt = 0; mode_attempt < 3; ++mode_attempt) {
        int change_mode = 0;
        hp4200_register_bank_init(&session->registers);
        if (session->sensor_bits == 8) session->registers.value[0x0c] = 0x60;
        /* Every CLI job is a fresh process, while HP's DLL can reuse its
         * initialized state. A live shading-RAM mismatch cleared on the
         * next complete initialization without a power cycle. Permit one
         * checked repeat after that specific failure; persistent mismatches
         * still fail before the lamp or scan motor is started. */
        unsigned ram_retries = 0;
        result = hp4200_initialize_core_mode_with_ram_retry(&session->io,
                    session->sensor_bits, &session->ram_diagnostic,
                    &ram_retries);
        session->ram_retries += ram_retries;
        if (result) return fail(session, 2, result);
        if (hp4200_bridge_activate(&session->io)) return fail(session, 3, -1);
        /* Core initialization writes the factory register image again,
         * including lamp control. Re-enable the lamp after each mode retry. */
        if (!session->lamp_on)
            lamp_started_ms = io->monotonic_ms(io->context);
        session->lamp_on = 1;
        if (hp4200_lamp_on(&session->io)) return fail(session, 4, -1);
        session->io.sleep_ms(session->io.context, 2000);
        for (;;) {
            int action;
            uint64_t elapsed;
            if (hp4200_probe_brightness(&session->io, &session->registers,
                                         &session->brightness))
                return fail(session, 5, -1);
            elapsed = io->monotonic_ms(io->context) - lamp_started_ms;
            action = hp4200_brightness_decide(session->brightness,
                                               zero_toggles, elapsed);
            if (action == HP4200_BRIGHTNESS_ACCEPT) break;
            if (action == HP4200_BRIGHTNESS_TOGGLE) {
                ++zero_toggles;
                session->sensor_bits = session->sensor_bits == 16 ? 8 : 16;
                change_mode = 1;
                break;
            }
            if (action == HP4200_BRIGHTNESS_FAIL ||
                elapsed >= HP4200_LAMP_WARMUP_LIMIT_MS)
                return fail(session, 5, (int)session->brightness);
            if (io->cancelled && io->cancelled(io->context))
                return fail(session, 5, -2);
            io->sleep_ms(io->context, 250);
        }
        if (change_mode) continue;
        break;
    }
    if (hp4200_home_start(&session->io)) return fail(session, 6, -1);
    if (hp4200_measure_origin(&session->io, &session->registers, 0x3e,
                               &session->origin_x, &session->origin_y))
        return fail(session, 7, -1);
    if (hp4200_measure_usb_rate(&session->io,
                                 &session->measured_kib_per_s))
        return fail(session, 8, -1);
    if (hp4200_home_wait_clear(&session->io)) return fail(session, 9, -1);

    if (hp4200_plan_rgb(request->x_dpi, request->y_dpi,
                         request->width_300, session->measured_kib_per_s,
                         &session->plan)) return fail(session, 10, -1);
    session->plan.near_bed_end =
        request->y_300 + request->height_300 > 3460;
    hp4200_timing_patch(&session->plan, &patch);
    hp4200_register_bank_apply(&session->registers, &patch);
    if (hp4200_write_color_calibration_setup(&session->io,
            &session->registers, session->origin_x,
            session->plan.sensor_x_dpi)) return fail(session, 11, -1);
    if (hp4200_calibrate_gain(&session->io,
            session->plan.sensor_x_dpi * 0x1feU / 10U, 1, 0,
            session->gain)) return fail(session, 12, -1);
    if (hp4200_write_offset_setup(&session->io, &session->registers,
                                   session->gain)) return fail(session, 13, -1);
    if (hp4200_calibrate_offset(&session->io,
            session->sensor_bits == 16, &session->offset))
        return fail(session, 14, -1);
    if (hp4200_calibrate_shading_rgb_mode(&session->io, &session->registers,
            session->origin_x, session->gain, session->offset.code,
            session->offset.shading_black, session->sensor_bits,
            session->plan.line_rate_factor,
            &session->shading)) return fail(session, 15, -1);
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (hp4200_build_asic_table(table, channel,
                session->offset.final_reference[channel],
                session->sensor_bits == 16) ||
            hp4200_upload_color_table(&session->io, channel, table))
            return fail(session, 16, (int)channel);
    }

    hp4200_timing_patch(&session->plan, &patch);
    if (request->y_300 < 40 ?
        hp4200_crop_patch_near_top(&patch, request->x_300,
            request->y_300, request->width_300, session->origin_x,
            session->origin_y, session->plan.motor_y_dpi, &near_top) :
        hp4200_crop_patch(&patch, request->x_300, request->y_300,
            request->width_300, session->origin_x, session->origin_y))
        return fail(session, 17, -1);
    hp4200_register_bank_apply(&session->registers, &patch);
    for (unsigned channel = 0; channel < 3; ++channel) {
        session->registers.value[0x38 + channel] = session->offset.code[channel];
        session->registers.value[0x3b + channel] = session->gain[channel];
    }
    session->registers.value[0x3e] = session->shading.reg3e;
    /* HP FUN_10002f50 retains a 1024-byte host gamma lookup for each
     * channel when FUN_10002d60's color matrix is enabled. The default
     * SetGammaTable state is the identity sequence installed by
     * FUN_100019a0. */
    hp4200_color_matrix(&session->color, session->sensor_bits == 16);
    for (unsigned i = 0; i < 256; ++i) identity[i] = (uint8_t)i;
    for (unsigned channel = 0; channel < 3; ++channel)
        hp4200_expand_256_to_1024(identity, session->color.table[channel]);
    result = request->y_300 < 40 ?
        hp4200_engine_begin_near_top(&session->engine, &session->io,
            &session->plan, &session->registers, request->height_300,
            &session->color, &near_top) :
        hp4200_engine_begin(&session->engine, &session->io, &session->plan,
            &session->registers, request->height_300, &session->color);
    if (result) return fail(session, 18, result);
    session->started = 1;
    return 0;
}


int hp4200_session_next_row(hp4200_session *session, uint8_t *output,
                             size_t capacity)
{
    int result;
    if (!session || !session->started) return -1;
    result = hp4200_engine_next_row(&session->engine, output, capacity);
    if (result < 0) {
        session->stage = 19;
        session->detail = result;
    }
    return result;
}

int hp4200_session_end(hp4200_session *session)
{
    int status = 0;
    int setup_failed;
    if (!session) return -1;
    if (session->ended) return 0;
    session->ended = 1;
    setup_failed = !session->started && session->stage;
    if (hp4200_engine_end(&session->engine)) status = -1;
    /* A scan read failure has already invoked motor_finish, which writes
     * command 0x02 to return home. Do not follow it with 0x00 and halt the
     * return. Setup failures still need an unconditional stop. */
    if ((status || setup_failed) && session->io.write_register &&
        hp4200_stop(&session->io)) status = -1;
    session->started = 0;
    if (session->lamp_on) {
        if (hp4200_lamp_off(&session->io)) status = -1;
        session->lamp_on = 0;
    }
    return status;
}
