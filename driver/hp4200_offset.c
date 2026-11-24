#include "hp4200_offset.h"

#include <stdlib.h>
#include <string.h>

static void move_positive(uint8_t *code)
{
    unsigned magnitude = *code & 0x1f;
    if ((*code & 0x20) == 0) {
        *code = (uint8_t)(magnitude < 31 ? magnitude + 1 : 31);
    } else if (magnitude == 0) {
        *code = 1;
    } else if (magnitude == 1) {
        *code = 0;
    } else {
        *code = (uint8_t)(0x20 | (magnitude - 1));
    }
}

void hp4200_offset_prepare(hp4200_offset_result *result, unsigned pass)
{
    if (!result || pass == 0 || pass > 5) return;
    uint8_t bit = (uint8_t)(0x20U >> pass);
    for (unsigned channel = 0; channel < 3; ++channel)
        result->code[channel] |= bit;
}

int hp4200_offset_observe(hp4200_offset_result *result, unsigned pass,
                           const uint8_t *samples, size_t length,
                           int special_sensor_mode)
{
    uint32_t sums[3] = {0};
    unsigned threshold = special_sensor_mode ? 0x80 : 0;
    if (!result || !samples || length < 0x60 || pass > 10) return -1;
    for (unsigned pixel = 0; pixel < 16; ++pixel)
        for (unsigned channel = 0; channel < 3; ++channel) {
            size_t offset = pixel * 6 + channel * 2;
            unsigned value = ((unsigned)samples[offset] << 8) |
                             samples[offset + 1];
            if (special_sensor_mode) value &= 0xfc;
            sums[channel] += value;
        }
    for (unsigned channel = 0; channel < 3; ++channel) {
        uint8_t bit = (uint8_t)(0x20U >> pass);
        if (pass == 0) {
            result->positive_reference[channel] = sums[channel] > threshold ? 0 : 1;
            if (sums[channel] > threshold) result->code[channel] |= bit;
        } else if (pass < 5) {
            int high = sums[channel] > threshold;
            if ((high && result->positive_reference[channel]) ||
                (!high && !result->positive_reference[channel]))
                result->code[channel] &= (uint8_t)~bit;
        } else if (pass == 5) {
            if (sums[channel] == 0) {
                if (!result->positive_reference[channel]) {
                    result->code[channel] &= (uint8_t)~bit;
                    if (result->code[channel] == 0x20) result->code[channel] = 0;
                } else {
                    move_positive(&result->code[channel]);
                }
            }
        } else {
            result->accumulated[channel] += sums[channel];
        }
    }
    if (pass == 5)
        for (unsigned channel = 0; channel < 3; ++channel)
            move_positive(&result->code[channel]);
    if (pass >= 6) {
        ++result->measurements;
        if (pass == 10) {
            for (unsigned channel = 0; channel < 3; ++channel) {
                uint32_t average = result->accumulated[channel] / 5;
                result->black_level[channel] = (uint16_t)(average >> 4);
                /* FUN_100072e0 stores black/4 (capped at 0x3f) at
                 * DAT_1000f0da/dc/de for shading, but derives the ASIC
                 * lookup reference from the unshifted black mean. */
                unsigned black = result->black_level[channel];
                unsigned shading = black >> 2;
                result->shading_black[channel] =
                    (uint16_t)(shading > 0x3f ? 0x3f : shading);
                result->final_reference[channel] = channel == 2 ?
                    (uint16_t)(((black * 0x4a / 100) + 0x34) / 10 + 1) :
                    (uint16_t)(((black * 0x47 / 100) + 0x2b) / 10 + 1);
            }
        }
    }
    return 0;
}

int hp4200_calibrate_offset(const hp4200_io *io, int special_sensor_mode,
                            hp4200_offset_result *result)
{
    uint8_t *samples;
    uint8_t reg45 = 0;
    int have_reg45 = 0;
    int status = -1;
    if (!io || !result || !io->read_register || !io->write_register ||
        !io->bulk_read || !io->monotonic_ms || !io->sleep_ms) return -1;
    samples = malloc(0x1c22);
    if (!samples) return -1;
    memset(result, 0, sizeof(*result));
    if (io->read_register(io->context, 0x45, &reg45)) goto done;
    have_reg45 = 1;
    if (io->write_register(io->context, 0x45, reg45 & 0xef)) goto done;
    for (unsigned pass = 0; pass <= 10; ++pass) {
        if (io->cancelled && io->cancelled(io->context)) { status = -2; goto done; }
        hp4200_offset_prepare(result, pass);
        for (unsigned channel = 0; channel < 3; ++channel)
            if (io->write_register(io->context, (uint8_t)(0x38 + channel),
                                   result->code[channel])) goto done;
        if (io->write_register(io->context, 0x07, 0x08) ||
            io->write_register(io->context, 0x07, 0x00)) goto done;
        io->sleep_ms(io->context, 50);
        if (io->write_register(io->context, 0x07, 0x03) ||
            hp4200_read_buffer(io, samples, 0x1c22) ||
            io->write_register(io->context, 0x07, 0x00)) goto done;
        if (hp4200_offset_observe(result, pass, samples, 0x1c22,
                                  special_sensor_mode)) goto done;
    }
    status = 0;
done:
    if (have_reg45 && io->write_register(io->context, 0x45, reg45)) status = -1;
    if (status) hp4200_stop(io);
    free(samples);
    return status;
}
