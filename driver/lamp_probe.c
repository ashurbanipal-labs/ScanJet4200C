#define _POSIX_C_SOURCE 200809L
#include "hp4200_brightness.h"
#include "hp4200_bridge.h"
#include "hp4200_home.h"
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
    hp4200_register_bank bank;
    struct sigaction action = {.sa_handler = request_stop};
    unsigned brightness = 0;
    int result = -1, lamp_on = 0, stage = 0, sensor_bits = 16;
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
    alarm(30);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        sensor_bits = attempt == 0 ? 16 : 8;
        stage = hp4200_initialize_core_mode(&io, (unsigned)sensor_bits);
        if (stage) goto done;
        stage = -20;
        if (hp4200_bridge_activate(&io)) goto done;
        lamp_on = 1;
        stage = -21;
        if (hp4200_lamp_on(&io)) goto done;
        io.sleep_ms(io.context, 2000);
        stage = -22;
        if (stop_requested || hp4200_probe_brightness(&io, &bank, &brightness))
            goto done;
        if (brightness != 0) { result = 0; break; }
        if (hp4200_lamp_off(&io)) goto done;
        lamp_on = 0;
    }
done:
    alarm(0);
    hp4200_stop(&io);
    if (lamp_on && hp4200_lamp_off(&io)) result = -1;
    hp4200_usb_close(&usb);
    if (result || stop_requested) {
        fprintf(stderr, "HP lamp brightness probe failed at stage %d%s\n",
                stage, stop_requested ? " (interrupted or timed out)" : "");
        return 1;
    }
    printf("HP lamp calibration-line mean: %u / 255 (%d-bit sensor mode)\n",
           brightness, sensor_bits);
    return 0;
}
