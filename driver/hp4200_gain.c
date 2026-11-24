#include "hp4200_gain.h"

#include <stdlib.h>

static int saturated(const uint8_t *samples, size_t sample_bytes,
                     int color_mode, unsigned channel)
{
    size_t stride = color_mode ? 6 : 2;
    for (size_t offset = color_mode ? channel * 2 : 0;
         offset + 1 < sample_bytes; offset += stride) {
        unsigned value = ((unsigned)samples[offset] << 8) | samples[offset + 1];
        if ((value & 0xfff0U) > 0xe5fU) return 1;
    }
    return 0;
}

int hp4200_calibrate_gain(const hp4200_io *io, size_t sample_bytes,
                          int color_mode, unsigned mono_channel,
                          uint8_t gain[3])
{
    uint8_t *samples;
    uint8_t reg45 = 0;
    int have_reg45 = 0;
    int result = -1;
    if (!io || !io->read_register || !io->write_register ||
        !io->bulk_read || !io->monotonic_ms || !io->sleep_ms || !gain ||
        sample_bytes < (color_mode ? 6U : 2U) || sample_bytes > 60000 ||
        mono_channel > 2) return -1;
    samples = malloc(sample_bytes + 2);
    if (!samples) return -1;
    if (io->read_register(io->context, 0x45, &reg45)) goto done;
    have_reg45 = 1;
    if (io->write_register(io->context, 0x45, reg45 & 0xef)) goto done;
    gain[0] = gain[1] = gain[2] = 0;
    for (unsigned bit = 0x20; bit; bit >>= 1) {
        if (io->cancelled && io->cancelled(io->context)) { result = -2; goto done; }
        for (unsigned channel = 0; channel < 3; ++channel) {
            if (!color_mode && channel != mono_channel) continue;
            gain[channel] |= (uint8_t)bit;
            if (io->write_register(io->context, (uint8_t)(0x3b + channel),
                                   gain[channel])) goto done;
        }
        /* FUN_10005c40: reset/stop the ASIC before each calibration line. */
        if (io->write_register(io->context, 0x07, 0x08) ||
            io->write_register(io->context, 0x07, 0x00)) goto done;
        io->sleep_ms(io->context, 50);
        if (io->write_register(io->context, 0x07, 0x03) ||
            hp4200_read_buffer(io, samples, sample_bytes + 2) ||
            io->write_register(io->context, 0x07, 0x00)) goto done;
        for (unsigned channel = 0; channel < 3; ++channel) {
            if (!color_mode && channel != mono_channel) continue;
            if (saturated(samples, sample_bytes, color_mode, channel))
                gain[channel] &= (uint8_t)~bit;
        }
    }
    result = 0;
done:
    /* Restore register 0x45 even after a failed calibration. */
    if (have_reg45 && io->write_register(io->context, 0x45, reg45)) result = -1;
    if (result) hp4200_stop(io);
    free(samples);
    return result;
}
