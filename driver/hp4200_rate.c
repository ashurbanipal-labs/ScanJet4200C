#include "hp4200_rate.h"

#include <stdlib.h>

int hp4200_measure_usb_rate(const hp4200_io *io, unsigned *kib_per_second)
{
    uint8_t *buffer, status;
    uint64_t start, elapsed;
    unsigned rate;
    if (!io || !io->read_register || !io->bulk_read ||
        !io->monotonic_ms || !kib_per_second) return -1;
    buffer = malloc(65536);
    if (!buffer) return -1;
    start = io->monotonic_ms(io->context);
    for (unsigned pass = 0; pass < 3; ++pass) {
        if (io->cancelled && io->cancelled(io->context)) {
            free(buffer);
            return -2;
        }
        /* FUN_100059c0 splits 0x10000 into 0xffff and one byte. */
        if (io->read_register(io->context, 1, &status) ||
            io->bulk_read(io->context, buffer, 65535) ||
            io->bulk_read(io->context, buffer + 65535, 1)) {
            free(buffer);
            return -1;
        }
    }
    elapsed = io->monotonic_ms(io->context) - start;
    free(buffer);
    if (!elapsed) return -1;
    rate = (unsigned)(3U * 65536U / elapsed);
    rate = rate > 20 ? rate - 20 : 0;
    if (rate < *kib_per_second) rate = *kib_per_second;
    if (rate < 650) rate = 650;
    if (rate > 750) rate = 750;
    *kib_per_second = rate;
    return 0;
}
