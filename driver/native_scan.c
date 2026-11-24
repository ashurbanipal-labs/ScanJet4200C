#define _POSIX_C_SOURCE 200809L
#include "hp4200_session.h"
#include "hp4200_square_pixels.h"
#include "hp4200_usb.h"
#include "png_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t stop_signal;

typedef struct {
    FILE *file;
    size_t rows;
} raw_capture;

static int capture_raw_line(void *context, size_t row,
                            const uint8_t *data, size_t length)
{
    raw_capture *capture = context;
    if (!capture->file || row != capture->rows ||
        fwrite(data, 1, length, capture->file) != length) return -1;
    ++capture->rows;
    return 0;
}

static void request_stop(int signal_number)
{
    stop_signal = signal_number;
    stop_requested = 1;
}

static int interrupted(void *context)
{
    (void)context;
    return stop_requested != 0;
}

static int number(const char *input, unsigned *output)
{
    char *end;
    unsigned long value;
    errno = 0;
    value = strtoul(input, &end, 10);
    if (errno || !input[0] || *end || value > 10000) return -1;
    *output = (unsigned)value;
    return 0;
}

static const char *read_failure(int status)
{
    switch (status) {
    case -2: return stop_signal == SIGALRM ? "scan time limit exceeded" :
                    "scan cancelled";
    case -3: return "scanner buffer-status read failed";
    case -4: return "no scanner image data for 15 seconds";
    case -5: return "USB image transfer failed";
    default: return "image row processing failed";
    }
}

static const char *setup_stage(int stage)
{
    static const char *const names[] = {
        "unknown", "scan geometry", "ASIC initialization", "USB bridge activation",
        "lamp startup", "lamp brightness", "home start", "optical origin",
        "USB rate measurement", "home wait", "scan timing plan",
        "color calibration setup", "analog gain", "offset setup",
        "black offset", "shading calibration", "color lookup upload",
        "scan crop", "motor start"
    };
    return stage > 0 && (unsigned)stage < sizeof(names) / sizeof(names[0]) ?
           names[stage] : names[0];
}

static void print_ram_mismatch(const char *phase,
                               const hp4200_ram_diagnostic *diagnostic)
{
    if (!diagnostic->has_mismatch) return;
    fprintf(stderr,
            "%s ASIC %s RAM channel %u, attempt %u: %zu mismatched bytes "
            "(first %zu, last %zu); first expected %02x, read %02x\n",
            phase, diagnostic->shading ? "shading" : "color",
            diagnostic->channel, diagnostic->attempt,
            diagnostic->mismatch_count, diagnostic->offset,
            diagnostic->last_offset, diagnostic->expected,
            diagnostic->actual);
    fprintf(stderr,
            "RAM mismatch pattern: %zu zeros, %zu matching previous byte, "
            "%zu matching next byte, %zu matching swapped word byte\n",
            diagnostic->zero_bytes, diagnostic->matches_previous,
            diagnostic->matches_next, diagnostic->matches_swapped);
}

static void print_usb_bulk_failure(const hp4200_usb *usb)
{
    if (!usb->bulk_requested) return;
    fprintf(stderr, "USB bulk %s port %u: %s, %d/%zu bytes transferred\n",
            usb->bulk_direction == 'R' ? "read" : "write",
            usb->bulk_port,
            usb->bulk_result ? libusb_error_name(usb->bulk_result) :
                               "short transfer",
            usb->bulk_transferred, usb->bulk_requested);
}

int main(int argc, char **argv)
{
    hp4200_scan_request request = {0};
    hp4200_session session = {0};
    hp4200_usb usb;
    hp4200_io io;
    hp4200_png_writer png = {0};
    hp4200_read_wait_diagnostic read_wait = {0};
    raw_capture capture = {0};
    struct sigaction action = {.sa_handler = request_stop};
    uint8_t *row = NULL, *display_row = NULL;
    FILE *output = NULL;
    size_t rows = 0, expected_rows;
    int status = 1, opened = 0, session_open = 0, output_created = 0;
    const char *extension = argc == 8 ? strrchr(argv[7], '.') : NULL;
    int png_output = extension && strcasecmp(extension, ".png") == 0;
    int ppm_output = extension && strcasecmp(extension, ".ppm") == 0;
    const char *host_buffer_setting = getenv("HP4200_HOST_BUFFER_MIB");
    const char *raw_capture_path = getenv("HP4200_RAW_CAPTURE");
    const char *square_setting = getenv("HP4200_SQUARE_PIXELS");
    int square_pixels = square_setting && strcmp(square_setting, "1") == 0;
    if (argc != 8 || number(argv[1], &request.x_300) ||
        number(argv[2], &request.y_300) ||
        number(argv[3], &request.width_300) ||
        number(argv[4], &request.height_300) ||
        number(argv[5], &request.x_dpi) ||
        number(argv[6], &request.y_dpi)) {
        fprintf(stderr,
                "Usage: %s X300 Y300 WIDTH300 HEIGHT300 XDPI YDPI OUTPUT.png|OUTPUT.ppm\n",
                argv[0]);
        return 2;
    }
    if (!png_output && !ppm_output) {
        fputs("Output filename must end in .png or .ppm\n", stderr);
        return 2;
    }
    if (host_buffer_setting && *host_buffer_setting &&
        strcmp(host_buffer_setting, "8") && strcmp(host_buffer_setting, "16") &&
        strcmp(host_buffer_setting, "128")) {
        fputs("HP4200_HOST_BUFFER_MIB must be 8, 16, or 128.\n", stderr);
        return 2;
    }
    if (square_pixels && request.y_dpi != request.x_dpi * 2) {
        fputs("Square-pixel output requires Y DPI to be twice X DPI.\n", stderr);
        return 2;
    }
    if (!request.height_300 || !request.y_dpi ||
        (size_t)request.height_300 * request.y_dpi >
        HP4200_MAX_Y_300 * 1200U)
        return 2;
    expected_rows = (size_t)request.height_300 * request.y_dpi / 300;
    if (!expected_rows) return 2;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
    int usb_open_result = hp4200_usb_open(&usb);
    if (usb_open_result) {
        fprintf(stderr, "ScanJet 4200C USB open failed: %s\n",
                libusb_error_name(usb_open_result));
        return 1;
    }
    opened = 1;
    hp4200_usb_make_io(&usb, &io);
    if (host_buffer_setting && !strcmp(host_buffer_setting, "16"))
        io.host_buffer_limit = 0x1000000;
    if (host_buffer_setting && !strcmp(host_buffer_setting, "128"))
        io.host_buffer_limit = 0x8000000;
    io.read_wait_diagnostic = &read_wait;
    const char *diagnostic = getenv("HP4200_DIAGNOSTIC");
    usb.diagnostic_scan = diagnostic && strcmp(diagnostic, "1") == 0;
    io.cancelled = interrupted;
    int output_fd = open(argv[7], O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (output_fd < 0) {
        perror(argv[7]);
        goto done;
    }
    output_created = 1;
    output = fdopen(output_fd, "wb");
    if (!output) {
        perror("fdopen");
        close(output_fd);
        goto done;
    }
    /* A full-bed 600 x 1200 dpi scan has up to 14,040 output rows. The
     * former 900-second cap could stop it even while data was arriving.
     * Keep the row-scaled overall bound; the read loop separately fails
     * after 15 seconds without scanner data. */
    unsigned max_seconds = 120 + (unsigned)(expected_rows / 10);
    alarm(max_seconds);
    if (hp4200_session_begin(&session, &io, &request)) {
        const char *stage_name =
            session.stage == 18 && session.detail == HP4200_ENGINE_NO_MEMORY ?
            "host scan buffer allocation" : setup_stage(session.stage);
        fprintf(stderr, "Native scan setup failed at stage %d (%s, detail %d)\n",
                session.stage, stage_name, session.detail);
        print_ram_mismatch("Startup", &session.ram_diagnostic);
        print_ram_mismatch("Final upload", &session.final_ram_diagnostic);
        print_usb_bulk_failure(&usb);
        goto done;
    }
    session_open = 1;
    fprintf(stderr,
            "Native scan: %u-bit sensor, lamp mean %u, USB %u KiB/s, %u x %zu pixels\n",
            session.sensor_bits, session.brightness,
            session.measured_kib_per_s, session.plan.output_pixels,
            expected_rows);
    if (session.ram_retries)
        fprintf(stderr, "ASIC RAM readback recovered after %u complete initialization retry\n",
                session.ram_retries);
    if (session.ram_diagnostic.write_retries)
        fprintf(stderr, "ASIC RAM readback recovered after %u plane rewrite%s\n",
                session.ram_diagnostic.write_retries,
                session.ram_diagnostic.write_retries == 1 ? "" : "s");
    fprintf(stderr, "HP final RAM uploads verified: 6 planes, %u rewrite%s\n",
            session.final_plane_rewrites,
            session.final_plane_rewrites == 1 ? "" : "s");
    fprintf(stderr,
            "HP calibration: gain %u/%u/%u, black %u/%u/%u, shading %u/%u/%u, ASIC reference %u/%u/%u, white %u..%u, reg3e=%02x\n",
            session.gain[0], session.gain[1], session.gain[2],
            session.offset.black_level[0], session.offset.black_level[1],
            session.offset.black_level[2],
            session.offset.shading_black[0], session.offset.shading_black[1],
            session.offset.shading_black[2],
            session.offset.final_reference[0],
            session.offset.final_reference[1],
            session.offset.final_reference[2],
            session.shading.minimum_white, session.shading.maximum_white,
            session.shading.reg3e);
    if (usb.diagnostic_scan)
        fprintf(stderr,
                "HP plan: reg08=%02x, motor step=%u, phase=%u, two-line=%u, raw line=%u bytes, host buffer=%zu bytes, reserve=%zu bytes\n",
                session.plan.reg08, session.plan.motor_step,
                session.plan.phase_difference, session.plan.two_line_mode,
                session.plan.transport_line_bytes,
                session.engine.stream_capacity,
                session.engine.stream_reserve);
    unsigned display_pixels = session.plan.output_pixels *
                              (square_pixels ? 2U : 1U);
    unsigned display_x_dpi = square_pixels ? request.y_dpi : request.x_dpi;
    if (raw_capture_path && *raw_capture_path) {
        int capture_fd = open(raw_capture_path, O_WRONLY | O_CREAT | O_EXCL,
                              0666);
        if (capture_fd >= 0) capture.file = fdopen(capture_fd, "wb");
        if (capture_fd >= 0 && !capture.file) close(capture_fd);
        if (capture.file && fprintf(capture.file,
                "HP4200RAW 1\n"
                "raw_pixels %u\noutput_pixels %u\nline_bytes %u\n"
                "raw_rows %zu\noutput_rows %zu\nstagger_rows %zu\n"
                "two_line_mode %u\nsensor_bits %u\n"
                "x_dpi %u\ny_dpi %u\nDATA\n",
                session.plan.raw_pixels, session.plan.output_pixels,
                session.plan.transport_line_bytes, session.engine.raw_rows,
                session.engine.output_rows, session.engine.stagger_rows,
                session.plan.two_line_mode, session.sensor_bits,
                request.x_dpi, request.y_dpi) < 0) {
            fclose(capture.file);
            capture.file = NULL;
            remove(raw_capture_path);
        }
        if (capture.file) {
            session.io.raw_observer_context = &capture;
            session.io.observe_raw_line = capture_raw_line;
        } else {
            fprintf(stderr, "Raw capture unavailable at %s: %s\n",
                    raw_capture_path, strerror(errno));
        }
    }
    usb.diagnostic_bulk_bytes = 0; /* Count image reads separately from calibration. */
    memset(&read_wait, 0, sizeof(read_wait));
    if (png_output && hp4200_png_begin(&png, output,
            display_pixels, (unsigned)expected_rows,
            display_x_dpi, request.y_dpi)) {
        fputs("Native scan output failed while starting PNG.\n", stderr);
        goto done;
    }
    if (ppm_output && fprintf(output, "P6\n%u %zu\n255\n",
                              display_pixels, expected_rows) < 0) {
        perror("Native scan output failed while starting PPM");
        goto done;
    }
    row = malloc(session.plan.output_pixels * 3);
    if (square_pixels) display_row = malloc((size_t)display_pixels * 3);
    if (!row || (square_pixels && !display_row)) {
        fputs("Native scan output failed: no memory for an image row.\n", stderr);
        goto done;
    }
    for (;;) {
        int next = hp4200_session_next_row(&session, row,
                                             session.plan.output_pixels * 3);
        if (next < 0) {
            fprintf(stderr, "Native scan read failed at stage %d (detail %d: %s)\n",
                    session.stage, session.detail, read_failure(next));
            if (next == -4)
                fprintf(stderr,
                        "Last scanner buffer status: %u KiB; longest low-buffer wait %llu ms\n",
                        read_wait.last_available_kib,
                        (unsigned long long)read_wait.longest_low_buffer_ms);
            print_usb_bulk_failure(&usb);
            goto done;
        }
        if (!next) break;
        if (square_pixels)
            hp4200_expand_horizontal_2x(row, session.plan.output_pixels,
                                         display_row);
        const uint8_t *saved_row = square_pixels ? display_row : row;
        if (png_output) {
            if (hp4200_png_row(&png, saved_row)) {
                fputs("Native scan output failed while writing PNG.\n", stderr);
                goto done;
            }
        } else if (fwrite(saved_row, 1, (size_t)display_pixels * 3, output) !=
                   (size_t)display_pixels * 3) {
            perror("Native scan output failed while writing PPM");
            goto done;
        }
        if (++rows % 100 == 0)
            fprintf(stderr, "Native scan: %zu/%zu rows\n", rows,
                    expected_rows);
    }
    if (rows != expected_rows || stop_requested) goto done;
    if (png_output ? hp4200_png_finish(&png) : fflush(output)) {
        fputs("Native scan output failed while finishing image.\n", stderr);
        goto done;
    }
    status = 0;
done:
    alarm(0);
    if (session_open && usb.diagnostic_scan)
        fprintf(stderr,
                "HP scan trace: %zu/%zu rows, %llu raw USB bytes, %llu ms reading, %llu ms max refill, %zu pause-request rows, %zu power-drop rows, motor running=%d\n",
                rows, expected_rows,
                (unsigned long long)usb.diagnostic_bulk_bytes,
                (unsigned long long)session.engine.read_ms,
                (unsigned long long)session.engine.max_read_ms,
                session.engine.pause_requests,
                session.engine.powerdrop_events,
                session.engine.motor_running);
    if (session_open && usb.diagnostic_scan)
        fprintf(stderr,
                "HP buffer wait: %zu low-buffer polls, %llu ms total, %llu ms longest, last status %u KiB\n",
                read_wait.low_buffer_polls,
                (unsigned long long)read_wait.total_low_buffer_ms,
                (unsigned long long)read_wait.longest_low_buffer_ms,
                read_wait.last_available_kib);
    if (session_open && usb.diagnostic_scan && session.engine.near_top)
        fprintf(stderr, "HP near-top marker: %s, alignment complete=%d\n",
                session.engine.top_marker_found ? "found" : "fallback",
                session.engine.top_ready);
    if (session_open && usb.diagnostic_scan && session.engine.pause_requests) {
        size_t count = session.engine.pause_requests < HP4200_PAUSE_HISTORY ?
                       session.engine.pause_requests : HP4200_PAUSE_HISTORY;
        fputs("HP pause-request raw rows:", stderr);
        for (size_t i = 0; i < count; ++i)
            fprintf(stderr, " %zu", session.engine.pause_rows[i]);
        if (count < session.engine.pause_requests) fputs(" ...", stderr);
        fputc('\n', stderr);
    }
    if (session_open && session.engine.powerdrop_events) {
        size_t count = session.engine.powerdrop_events < HP4200_PAUSE_HISTORY ?
                       session.engine.powerdrop_events : HP4200_PAUSE_HISTORY;
        fprintf(stderr, "HP scanner power-drop status on %zu raw row%s:",
                session.engine.powerdrop_events,
                session.engine.powerdrop_events == 1 ? "" : "s");
        for (size_t i = 0; i < count; ++i)
            fprintf(stderr, " %zu", session.engine.powerdrop_rows[i]);
        if (count < session.engine.powerdrop_events) fputs(" ...", stderr);
        fputc('\n', stderr);
    }
    free(display_row);
    free(row);
    if (session_open && hp4200_session_end(&session)) status = 1;
    if (capture.file) {
        int capture_close_failed = fclose(capture.file);
        if (session.engine.raw_capture_error || capture_close_failed) {
            fprintf(stderr, "Raw capture failed after %zu rows; removing %s\n",
                    capture.rows, raw_capture_path);
            remove(raw_capture_path);
        } else {
            fprintf(stderr, "Raw capture: %zu rows saved to %s\n",
                    capture.rows, raw_capture_path);
        }
    }
    hp4200_png_destroy(&png);
    if (output && fclose(output)) {
        perror("Native scan output failed while closing image");
        status = 1;
    }
    if (status && output_created) remove(argv[7]);
    if (opened) hp4200_usb_close(&usb);
    if (status && stop_requested)
        fprintf(stderr, "Native scan %s\n",
                stop_signal == SIGALRM ? "time limit exceeded" : "interrupted");
    return status;
}
