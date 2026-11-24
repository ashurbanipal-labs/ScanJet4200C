#include "hp4200_final_capture.h"
#include "hp4200_calibration_setup.h"
#include "hp4200_final_calibration.h"

#include <stdlib.h>

enum { CHANNEL_SAMPLES = 5100, LINE_SAMPLES = CHANNEL_SAMPLES * 3,
       LINE_BYTES = LINE_SAMPLES * 2 + 2, LINE_COUNT = 9,
       SHADING_RAM_WORDS = 0x1554 };

int hp4200_calibrate_shading_rgb_mode(const hp4200_io *io,
                                 const hp4200_register_bank *bank,
                                 unsigned origin_x,
                                 const uint8_t gain[3],
                                 const uint8_t offset[3],
                                 const uint16_t shading_black[3],
                                 unsigned sensor_bits,
                                 unsigned sample_group,
                                 hp4200_shading_result *result)
{
    uint8_t *raw = NULL, *planes[3] = {NULL, NULL, NULL};
    uint16_t *white = NULL;
    unsigned factory_x, minimum = 0xffff, maximum = 0;
    size_t left_pad, plane_bytes;
    int status = -1;
    if (!io || !bank || !gain || !offset || !shading_black || !result ||
        (sensor_bits != 8 && sensor_bits != 16) ||
        (sample_group != 1 && sample_group != 2) ||
        !io->write_register || !io->read_register || !io->bulk_read ||
        !io->bulk_write_ram || !io->monotonic_ms || !io->sleep_ms) return -1;
    factory_x = ((unsigned)bank->value[0x1e] << 8) | bank->value[0x1f];
    /* FUN_10008450 prefixes the 5,100 captured sensor samples with the
     * origin offset. The LM9830 plane holds only 0x1554 words. */
    if (origin_x < factory_x ||
        origin_x - factory_x > SHADING_RAM_WORDS - CHANNEL_SAMPLES)
        return -1;
    left_pad = origin_x - factory_x;
    plane_bytes = (left_pad + CHANNEL_SAMPLES) * 2;
    raw = malloc(LINE_BYTES * LINE_COUNT);
    white = malloc(LINE_SAMPLES * sizeof(*white));
    for (unsigned channel = 0; channel < 3; ++channel)
        planes[channel] = malloc(plane_bytes);
    if (!raw || !white || !planes[0] || !planes[1] || !planes[2]) goto done;
    if (hp4200_write_shading_setup(io, bank, origin_x, gain, offset) ||
        io->write_register(io->context, 7, 8) ||
        io->write_register(io->context, 7, 0)) goto done;
    io->sleep_ms(io->context, 50);
    if (io->cancelled && io->cancelled(io->context)) goto done;
    if (io->write_register(io->context, 7, 3) ||
        hp4200_read_buffer(io, raw, LINE_BYTES * LINE_COUNT) ||
        io->write_register(io->context, 7, 0)) goto done;
    if (hp4200_reduce_nine_lines(raw, LINE_BYTES, LINE_SAMPLES, white))
        goto done;
    for (size_t i = 0; i < LINE_SAMPLES; ++i) {
        if (white[i] < minimum) minimum = white[i];
        if (white[i] > maximum) maximum = white[i];
    }
    result->minimum_white = minimum;
    result->maximum_white = maximum;
    if (hp4200_make_rgb_shading_mode(white, CHANNEL_SAMPLES, left_pad,
                                     planes, &result->reg3e, shading_black,
                                     sensor_bits, sample_group) ||
        hp4200_upload_rgb_shading(io, planes, plane_bytes)) goto done;
    result->plane_bytes = plane_bytes;
    status = 0;
done:
    if (status) hp4200_stop(io);
    for (unsigned channel = 0; channel < 3; ++channel) free(planes[channel]);
    free(white);
    free(raw);
    return status;
}

int hp4200_calibrate_shading_rgb(const hp4200_io *io,
                                 const hp4200_register_bank *bank,
                                 unsigned origin_x,
                                 const uint8_t gain[3],
                                 const uint8_t offset[3],
                                 hp4200_shading_result *result)
{
    static const uint16_t no_black[3] = {0, 0, 0};
    return hp4200_calibrate_shading_rgb_mode(io, bank, origin_x, gain,
                                             offset, no_black, 8, 1, result);
}
