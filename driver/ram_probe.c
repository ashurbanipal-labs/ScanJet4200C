#include "hp4200_usb.h"

#include <stdio.h>
#include <string.h>

enum { SHADING_BYTES = 0x2aa8 };

int main(void)
{
    hp4200_usb usb;
    hp4200_io io;
    uint8_t saved[3], expected[SHADING_BYTES], actual[SHADING_BYTES];
    int status = 1;
    if (hp4200_usb_open(&usb)) {
        fputs("ScanJet 4200C USB open failed\n", stderr);
        return 1;
    }
    hp4200_usb_make_io(&usb, &io);
    for (unsigned reg = 3; reg <= 5; ++reg)
        if (io.read_register(io.context, reg, &saved[reg - 3])) {
            fputs("Could not save ASIC RAM address registers\n", stderr);
            goto done;
        }
    for (unsigned word = 0; word < SHADING_BYTES / 2; ++word) {
        unsigned value = word & 0x3ff;
        expected[word * 2] = (uint8_t)(value >> 8);
        expected[word * 2 + 1] = (uint8_t)value;
    }
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (io.write_register(io.context, 3, (uint8_t)(channel * 2 + 1)) ||
            io.write_register(io.context, 4, 0x20) ||
            io.write_register(io.context, 5, 0) ||
            io.bulk_read_ram(io.context, 6, actual, sizeof(actual))) {
            fprintf(stderr, "Shading RAM channel %u read failed\n", channel);
            goto restore;
        }
        size_t mismatch = 0, zero = 0, first = SHADING_BYTES;
        for (size_t i = 0; i < SHADING_BYTES; ++i) {
            if (actual[i] == 0) ++zero;
            if (actual[i] != expected[i]) {
                if (first == SHADING_BYTES) first = i;
                ++mismatch;
            }
        }
        printf("shading %u: mismatches=%zu first=%zu zeros=%zu prefix=",
               channel, mismatch, first, zero);
        for (size_t i = 0; i < 32; ++i) printf("%02x", actual[i]);
        putchar('\n');
        if (mismatch)
            for (size_t start = 0; start < SHADING_BYTES; start += 1024) {
                size_t end = start + 1024;
                size_t matches = 0, nonzero = 0;
                if (end > SHADING_BYTES) end = SHADING_BYTES;
                for (size_t i = start; i < end; ++i) {
                    if (actual[i] == expected[i]) ++matches;
                    if (actual[i]) ++nonzero;
                }
                printf("  bytes %5zu..%5zu: matches=%4zu nonzero=%4zu\n",
                       start, end - 1, matches, nonzero);
            }
        if (mismatch)
            for (size_t start = 0; start < SHADING_BYTES; start += 1024) {
                printf("  sample %5zu: ", start);
                for (size_t i = start; i < start + 16 && i < SHADING_BYTES; ++i)
                    printf("%02x", actual[i]);
                putchar('\n');
            }
    }
    status = 0;
restore:
    for (unsigned reg = 3; reg <= 5; ++reg)
        if (io.write_register(io.context, reg, saved[reg - 3])) status = 1;
done:
    hp4200_usb_close(&usb);
    return status;
}
