#include "hp4200_brightness.h"

#include <stdlib.h>

int hp4200_brightness_decide(unsigned mean, unsigned zero_toggles,
                              uint64_t elapsed_ms)
{
    if (mean > 255) return HP4200_BRIGHTNESS_FAIL;
    if (mean == 0)
        return zero_toggles < 2 ? HP4200_BRIGHTNESS_TOGGLE :
                                  HP4200_BRIGHTNESS_FAIL;
    if (mean < 20 && elapsed_ms > 3000)
        return HP4200_BRIGHTNESS_FAIL;
    return mean < 130 ? HP4200_BRIGHTNESS_RETRY :
                        HP4200_BRIGHTNESS_ACCEPT;
}

int hp4200_probe_brightness(const hp4200_io *io,
                             const hp4200_register_bank *factory,
                             unsigned *mean_brightness)
{
    uint8_t reg45 = 0, *line;
    unsigned start, end;
    size_t length;
    unsigned long sum = 0;
    int status = -1, have_reg45 = 0;
    if (!io || !factory || !mean_brightness || !io->read_register ||
        !io->write_register || !io->bulk_read || !io->monotonic_ms ||
        !io->sleep_ms) return -1;
    start = ((unsigned)factory->value[0x22] << 8) | factory->value[0x23];
    end = ((unsigned)factory->value[0x24] << 8) | factory->value[0x25];
    if (end <= start || end - start > 0x2000) return -1;
    length = (end - start) * 3 + 1;
    line = malloc(length);
    if (!line) return -1;
    if (io->read_register(io->context, 0x45, &reg45)) goto done;
    have_reg45 = 1;
    if (io->write_register(io->context, 0x45, reg45 & 0xef) ||
        io->write_register(io->context, 7, 8) ||
        io->write_register(io->context, 7, 0)) goto done;
    io->sleep_ms(io->context, 50);
    if (io->cancelled && io->cancelled(io->context)) goto done;
    if (io->write_register(io->context, 7, 3) ||
        hp4200_read_buffer(io, line, length) ||
        io->write_register(io->context, 7, 0)) goto done;
    for (size_t i = 0; i < length - 1; ++i) sum += line[i];
    *mean_brightness = (unsigned)(sum / (length - 1));
    status = 0;
done:
    if (have_reg45 && io->write_register(io->context, 0x45, reg45))
        status = -1;
    if (status) hp4200_stop(io);
    free(line);
    return status;
}
