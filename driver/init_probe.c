#define _POSIX_C_SOURCE 200809L
#include "hp4200_initialize.h"
#include "hp4200_usb.h"

#include <signal.h>
#include <stdio.h>
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

int main(void)
{
    hp4200_usb usb;
    hp4200_io io;
    hp4200_ram_diagnostic diagnostic = {0};
    struct sigaction action = {.sa_handler = request_stop};
    uint8_t sensor, status;
    int result;
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
    alarm(45);
    result = hp4200_initialize_core_mode_diagnose(&io, 16, &diagnostic);
    alarm(0);
    if (result || stop_requested) {
        hp4200_stop(&io);
        fprintf(stderr, "HP core initialization failed at stage %d%s\n",
                result, stop_requested ? " (interrupted or timed out)" : "");
        if (diagnostic.has_mismatch)
            fprintf(stderr,
                    "ASIC %s RAM channel=%u attempt=%u mismatches=%zu "
                    "first=%zu last=%zu expected=%02x actual=%02x "
                    "zeros=%zu previous=%zu next=%zu swapped=%zu\n",
                    diagnostic.shading ? "shading" : "color",
                    diagnostic.channel, diagnostic.attempt,
                    diagnostic.mismatch_count, diagnostic.offset,
                    diagnostic.last_offset, diagnostic.expected,
                    diagnostic.actual, diagnostic.zero_bytes,
                    diagnostic.matches_previous, diagnostic.matches_next,
                    diagnostic.matches_swapped);
        hp4200_usb_close(&usb);
        return 1;
    }
    if (io.read_register(io.context, 2, &sensor) ||
        io.read_register(io.context, 0x69, &status)) {
        fputs("Post-initialization status read failed\n", stderr);
        hp4200_usb_close(&usb);
        return 1;
    }
    printf("HP core initialization passed; sensor=0x%02x ASIC=0x%02x\n",
           sensor, status);
    if (diagnostic.write_retries)
        printf("ASIC RAM recovered after %u plane rewrite%s\n",
               diagnostic.write_retries,
               diagnostic.write_retries == 1 ? "" : "s");
    hp4200_usb_close(&usb);
    return 0;
}
