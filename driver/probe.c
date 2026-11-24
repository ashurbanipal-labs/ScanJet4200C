#include "hp4200_usb.h"
#include "hp4200_bridge.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    hp4200_usb usb;
    hp4200_io io;
    uint8_t value, asic_status, home_status;
    int activate = argc == 2 && strcmp(argv[1], "--activate-bridge") == 0;
    int dump = argc == 2 && strcmp(argv[1], "--dump-registers") == 0;
    if (argc > 2 || (argc == 2 && !activate && !dump)) {
        fprintf(stderr, "Usage: %s [--activate-bridge|--dump-registers]\n", argv[0]);
        return 2;
    }
    int rc = hp4200_usb_open(&usb);
    if (rc) {
        fprintf(stderr, "ScanJet 4200C USB open failed: %d\n", rc);
        return 1;
    }
    hp4200_usb_make_io(&usb, &io);
    if (activate && hp4200_bridge_activate(&io)) {
        fprintf(stderr, "HP PV8630 bridge activation failed\n");
        hp4200_usb_close(&usb);
        return 1;
    }
    if (dump) {
        static const uint8_t addresses[] = {
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0f, 0x10, 0x11, 0x12, 0x13,
            0x14, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
            0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
            0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x58, 0x5a, 0x5c, 0x5d,
            0x5e
        };
        for (size_t i = 0; i < sizeof(addresses); ++i) {
            if (io.read_register(io.context, addresses[i], &value)) {
                rc = -1;
                break;
            }
            printf("%02x=%02x%c", addresses[i], value,
                   i + 1 == sizeof(addresses) ? '\n' : ' ');
        }
        hp4200_usb_close(&usb);
        return rc ? 1 : 0;
    }
    rc = io.read_register(io.context, 0x01, &value);
    if (rc) fprintf(stderr, "LM9830 status register read failed\n");
    else {
        printf("LM9830 buffer status (reg 01): 0x%02x\n", value);
        rc = io.read_register(io.context, 0x69, &asic_status);
        if (rc) fprintf(stderr, "LM9830 ASIC status register read failed\n");
        else {
            printf("LM9830 ASIC status (reg 69): 0x%02x\n", asic_status);
            rc = io.read_register(io.context, 2, &home_status);
            if (rc) fprintf(stderr, "LM9830 home status read failed\n");
            else printf("LM9830 home status (reg 02): 0x%02x\n", home_status);
        }
    }
    hp4200_usb_close(&usb);
    return rc ? 1 : 0;
}
