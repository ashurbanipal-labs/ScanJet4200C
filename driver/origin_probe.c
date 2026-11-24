#define _POSIX_C_SOURCE 200809L
#include "hp4200_bridge.h"
#include "hp4200_brightness.h"
#include "hp4200_calibration_setup.h"
#include "hp4200_engine.h"
#include "hp4200_gain.h"
#include "hp4200_final_capture.h"
#include "hp4200_gamma.h"
#include "hp4200_home.h"
#include "hp4200_initialize.h"
#include "hp4200_motor.h"
#include "hp4200_offset.h"
#include "hp4200_origin_scan.h"
#include "hp4200_plan.h"
#include "hp4200_rate.h"
#include "hp4200_registers.h"
#include "hp4200_usb.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int interrupted(void *context)
{
    (void)context;
    return stop_requested != 0;
}

int main(int argc, char **argv)
{
    hp4200_usb usb;
    hp4200_io io;
    hp4200_register_bank bank;
    struct sigaction action = {.sa_handler = request_stop};
    uint16_t origin_x = 0, origin_y = 0;
    unsigned brightness = 0, rate = 0;
    uint8_t gain[3] = {0};
    hp4200_offset_result offset_result = {0};
    hp4200_shading_result shading_result = {0};
    hp4200_engine engine = {0};
    FILE *scan_file = NULL;
    uint8_t *scan_row = NULL;
    size_t scan_rows = 0;
    int stage = 0, lamp_on = 0;
    int wide_strip_600 = argc == 2 &&
        strcmp(argv[1], "--wide-strip-600") == 0;
    int small_scan_600 = wide_strip_600 || (argc == 2 &&
        strcmp(argv[1], "--small-scan-600") == 0);
    int small_scan = small_scan_600 ||
        (argc == 2 && strcmp(argv[1], "--small-scan") == 0);
    unsigned scan_dpi = small_scan_600 ? 600 : 300;
    unsigned scan_width = wide_strip_600 ? 2480 : 236;
    unsigned scan_height = wide_strip_600 ? 5 : 236;
    const char *scan_path = wide_strip_600 ?
        "work/native-tests/native-600-wide-strip.ppm" : small_scan_600 ?
        "work/native-tests/native-600-small.ppm" :
        "work/native-tests/native-300-small.ppm";
    int calibrate_shading = small_scan || (argc == 2 &&
        strcmp(argv[1], "--calibrate-shading") == 0);
    int calibrate_offset = calibrate_shading || (argc == 2 &&
        strcmp(argv[1], "--calibrate-offset") == 0);
    int calibrate_gain = calibrate_offset ||
        (argc == 2 && strcmp(argv[1], "--calibrate-gain") == 0);
    int measure_rate = calibrate_gain ||
        (argc == 2 && strcmp(argv[1], "--measure-rate") == 0);
    if (argc > 2 || (argc == 2 && !measure_rate)) {
        fprintf(stderr, "Usage: %s [--measure-rate|--calibrate-gain|--calibrate-offset|--calibrate-shading|--small-scan|--small-scan-600|--wide-strip-600]\n", argv[0]);
        return 2;
    }
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
    if (hp4200_usb_open(&usb)) {
        fputs("ScanJet 4200C USB open failed\n", stderr);
        return 1;
    }
    hp4200_usb_make_io(&usb, &io);
    io.cancelled = interrupted;
    hp4200_register_bank_init(&bank);
    bank.value[0x0c] = 0x60;
    alarm(wide_strip_600 ? 120 : small_scan ? 60 : 45);
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        stage = hp4200_initialize_core_mode(&io, 8);
        if (!stage || stage > -1100 || stage < -1199 || stop_requested)
            break;
        io.sleep_ms(io.context, 100);
    }
    if (stage) goto done;
    stage = -20;
    if (hp4200_bridge_activate(&io)) goto done;
    lamp_on = 1;
    stage = -21;
    if (hp4200_lamp_on(&io)) goto done;
    io.sleep_ms(io.context, 2000);
    stage = -22;
    if (hp4200_probe_brightness(&io, &bank, &brightness) ||
        brightness < 130) goto done;
    stage = -23;
    if (hp4200_home_start(&io)) goto done;
    stage = -24;
    if (hp4200_measure_origin(&io, &bank, 0x3e,
                              &origin_x, &origin_y)) goto done;
    if (measure_rate) {
        stage = -25;
        if (hp4200_measure_usb_rate(&io, &rate)) goto done;
    }
    stage = -26;
    if (hp4200_home_wait_clear(&io)) goto done;
    if (calibrate_gain) {
        hp4200_scan_plan plan;
        hp4200_register_patch patch;
        unsigned dpi = small_scan ? scan_dpi :
                       calibrate_shading ? 300 : 75;
        stage = -28;
        if (hp4200_plan_rgb(dpi, dpi, scan_width, rate, &plan)) goto done;
        hp4200_timing_patch(&plan, &patch);
        hp4200_register_bank_apply(&bank, &patch);
        stage = -29;
        if (hp4200_write_color_calibration_setup(&io, &bank,
                                                  origin_x, dpi)) goto done;
        stage = -30;
        if (hp4200_calibrate_gain(&io, dpi * 0x1feU / 10U,
                                  1, 0, gain)) goto done;
        fprintf(stderr, "HP RGB analog gains measured: %u %u %u\n",
                gain[0], gain[1], gain[2]);
        if (calibrate_offset) {
            stage = -32;
            if (hp4200_write_offset_setup(&io, &bank, gain)) goto done;
            stage = -33;
            if (hp4200_calibrate_offset(&io, 0, &offset_result)) goto done;
        }
        if (calibrate_shading) {
            stage = -34;
            if (hp4200_calibrate_shading_rgb(&io, &bank, origin_x,
                                              gain, offset_result.code,
                                              &shading_result)) goto done;
        }
        if (small_scan) {
            hp4200_scan_plan plan;
            hp4200_register_patch patch;
            uint8_t table[1024];
            stage = -40;
            for (unsigned channel = 0; channel < 3; ++channel) {
                if (hp4200_build_asic_table(table, channel,
                       offset_result.final_reference[channel], 0) ||
                    hp4200_upload_color_table(&io, channel, table)) goto done;
            }
            if (hp4200_plan_rgb(scan_dpi, scan_dpi, scan_width, rate, &plan)) goto done;
            hp4200_timing_patch(&plan, &patch);
            if (hp4200_crop_patch(&patch, 0, 120, scan_width,
                                   origin_x, origin_y)) goto done;
            hp4200_register_bank_apply(&bank, &patch);
            for (unsigned channel = 0; channel < 3; ++channel) {
                bank.value[0x38 + channel] = offset_result.code[channel];
                bank.value[0x3b + channel] = gain[channel];
            }
            bank.value[0x3e] = shading_result.reg3e;
            stage = -41;
            scan_file = fopen(scan_path, "wb");
            if (!scan_file) goto done;
            if (fprintf(scan_file, "P6\n%u %u\n255\n",
                        plan.output_pixels,
                        scan_height * scan_dpi / 300U) < 0) goto done;
            scan_row = malloc(plan.output_pixels * 3);
            if (!scan_row) goto done;
            usb.diagnostic_scan = wide_strip_600;
            usb.diagnostic_next_ms = 0;
            usb.diagnostic_bulk_bytes = 0;
            stage = -42;
            if (hp4200_engine_begin(&engine, &io, &plan, &bank, scan_height,
                                     NULL)) goto done;
            stage = -43;
            for (;;) {
                int next = hp4200_engine_next_row(&engine, scan_row,
                                                   plan.output_pixels * 3);
                if (next < 0) goto done;
                if (next == 0) break;
                if (fwrite(scan_row, 1, plan.output_pixels * 3, scan_file) !=
                    plan.output_pixels * 3) goto done;
                ++scan_rows;
                if (wide_strip_600 && (scan_rows <= 3 || scan_rows % 5 == 0))
                    fprintf(stderr, "wide strip: %zu/%u rows, %zu raw, %llu total read ms, %llu max read ms, %zu status flags, alignment %d\n",
                            scan_rows, scan_height * scan_dpi / 300,
                            engine.raw_seen,
                            (unsigned long long)engine.read_ms,
                            (unsigned long long)engine.max_read_ms,
                            engine.pause_requests, engine.two_line_shift);
            }
            stage = -44;
            if (scan_rows != scan_height * scan_dpi / 300U ||
                hp4200_engine_end(&engine)) goto done;
            if (fclose(scan_file)) { scan_file = NULL; goto done; }
            scan_file = NULL;
        } else {
            stage = -31;
            if (hp4200_motor_finish(&io)) goto done;
        }
    }
    stage = 0;
done:
    alarm(0);
    hp4200_engine_end(&engine);
    free(scan_row);
    if (scan_file) fclose(scan_file);
    if (stage || stop_requested)
        remove(scan_path);
    if (stage || stop_requested) hp4200_stop(&io);
    if (lamp_on && hp4200_lamp_off(&io)) stage = -27;
    hp4200_usb_close(&usb);
    if (stage || stop_requested) {
        if (stage == -34)
            fprintf(stderr, "Reduced white range: %u..%u\n",
                    shading_result.minimum_white,
                    shading_result.maximum_white);
        fprintf(stderr, "HP origin probe failed at stage %d%s\n",
                stage, stop_requested ? " (interrupted or timed out)" : "");
        return 1;
    }
    printf("HP origin: x=%u y=%u; lamp mean=%u / 255\n",
           origin_x, origin_y, brightness);
    if (measure_rate) printf("HP measured USB rate: %u KiB/s\n", rate);
    if (calibrate_gain)
        printf("HP RGB analog gains: %u %u %u\n",
               gain[0], gain[1], gain[2]);
    if (calibrate_offset)
        printf("HP RGB offsets: %u %u %u; black levels: %u %u %u\n",
               offset_result.code[0], offset_result.code[1],
               offset_result.code[2], offset_result.black_level[0],
               offset_result.black_level[1], offset_result.black_level[2]);
    if (calibrate_shading)
        printf("HP RGB shading: mode 0x%02x, white %u..%u, %zu bytes/plane\n",
               shading_result.reg3e, shading_result.minimum_white,
               shading_result.maximum_white, shading_result.plane_bytes);
    if (small_scan)
        printf("Native %u dpi image: %s (%zu rows)\n",
               scan_dpi, scan_path, scan_rows);
    return 0;
}
