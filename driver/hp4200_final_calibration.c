#include "hp4200_final_calibration.h"
#include "hp4200_ram.h"

#include <limits.h>
#include <string.h>

int hp4200_reduce_nine_lines(const uint8_t *raw, size_t stride_bytes,
                              size_t samples_per_line, uint16_t *output)
{
    if (!raw || !output || !samples_per_line ||
        stride_bytes < samples_per_line * 2) return -1;
    for (size_t sample = 0; sample < samples_per_line; ++sample) {
        uint16_t values[9];
        for (unsigned line = 0; line < 9; ++line) {
            size_t offset = line * stride_bytes + sample * 2;
            unsigned original = ((unsigned)raw[offset] << 8) | raw[offset + 1];
            values[line] = (uint16_t)((original >> 2) & 0x3ff);
        }
        /* The DLL selection-sorts only the four smallest values. */
        for (unsigned low = 0; low < 4; ++low)
            for (unsigned other = low + 1; other < 9; ++other)
                if (values[other] < values[low]) {
                    uint16_t temporary = values[low];
                    values[low] = values[other];
                    values[other] = temporary;
                }
        unsigned sum = 0;
        for (unsigned keep = 4; keep < 9; ++keep) sum += values[keep];
        output[sample] = (uint16_t)(sum / 5);
    }
    return 0;
}

int hp4200_make_rgb_shading_mode(const uint16_t *white,
                                 size_t samples_per_channel, size_t left_pad,
                                 uint8_t *plane_bytes[3], uint8_t *reg3e,
                                 const uint16_t shading_black[3],
                                 unsigned sensor_bits,
                                 unsigned sample_group)
{
    unsigned minimum = UINT_MAX, scale, mode, ceiling;
    size_t samples, plane_length;
    if (!white || !plane_bytes || !reg3e || !shading_black ||
        (sensor_bits != 8 && sensor_bits != 16) ||
        (sample_group != 1 && sample_group != 2) || !samples_per_channel ||
        samples_per_channel > SIZE_MAX / 3 ||
        left_pad > SIZE_MAX / 2 - samples_per_channel ||
        !plane_bytes[0] || !plane_bytes[1] || !plane_bytes[2]) return -1;
    samples = samples_per_channel * 3;
    plane_length = (left_pad + samples_per_channel) * 2;
    for (size_t i = 0; i < samples; ++i) {
        unsigned black = sensor_bits == 16 ? shading_black[i % 3] : 0;
        if (white[i] <= black || white[i] > 0x3ff) return -1;
        if (white[i] - black < minimum) minimum = white[i] - black;
    }
    /* HP chooses one of three hardware gain scales from the darkest white
     * sample and stores the corresponding mode in register 0x3e. */
    if (0x7feU / minimum < 3) { scale = 0x800; mode = 8; }
    else if (0x7feU / minimum < 4) { scale = 0x400; mode = 9; }
    else { scale = 0x200; mode = 10; }
    if (sensor_bits == 16) mode |= 0x10;
    *reg3e = (uint8_t)mode;
    ceiling = sensor_bits == 16 ? 0x3d4 : 0x3ff;
    for (unsigned channel = 0; channel < 3; ++channel)
        memset(plane_bytes[channel], 0, plane_length);
    for (size_t pixel = 0; pixel < samples_per_channel; ++pixel)
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned black = sensor_bits == 16 ? shading_black[channel] : 0;
            unsigned sample = white[pixel * 3 + channel] - black;
            unsigned correction;
            size_t offset = (left_pad + pixel) * 2;
            if (sample > ceiling) sample = ceiling;
            correction = ((ceiling - sample) * scale) / sample;
            if (correction > ceiling) correction = ceiling;
            plane_bytes[channel][offset] = (uint8_t)(correction >> 8);
            plane_bytes[channel][offset + 1] = (uint8_t)correction;
        }
    for (unsigned channel = 0; channel < 3; ++channel) {
        uint8_t *plane = plane_bytes[channel];
        size_t words = left_pad + samples_per_channel;
        /* HP FUN_10007b50 averages adjacent shading words in place when
         * DAT_1000f072 is 2. It uploads the original plane length; only
         * the leading grouped words are used at this sensor rate. */
        if (sample_group == 2)
            for (size_t output = 0; output < words / 2; ++output) {
                size_t input = output * 4;
                unsigned first = ((unsigned)plane[input] << 8) | plane[input + 1];
                unsigned second = ((unsigned)plane[input + 2] << 8) |
                                  plane[input + 3];
                unsigned averaged = (first + second) / 2;
                plane[output * 2] = (uint8_t)(averaged >> 8);
                plane[output * 2 + 1] = (uint8_t)averaged;
            }
        if (sensor_bits == 16)
            for (size_t pixel = 0; pixel < words; ++pixel) {
                unsigned value = ((unsigned)plane[pixel * 2] << 8) |
                                 plane[pixel * 2 + 1];
                value |= (unsigned)shading_black[channel] << 2;
                plane[pixel * 2] = (uint8_t)(value >> 8);
                plane[pixel * 2 + 1] = (uint8_t)value;
            }
    }
    return 0;
}

int hp4200_make_rgb_shading(const uint16_t *white, size_t samples_per_channel,
                            size_t left_pad, uint8_t *plane_bytes[3],
                            uint8_t *reg3e)
{
    static const uint16_t no_black[3] = {0, 0, 0};
    return hp4200_make_rgb_shading_mode(white, samples_per_channel,
                                        left_pad, plane_bytes, reg3e,
                                        no_black, 8, 1);
}

int hp4200_upload_rgb_shading(const hp4200_io *io,
                              uint8_t *const plane_bytes[3], size_t length)
{
    if (!plane_bytes || !length || length > 65535) return -1;
    for (unsigned channel = 0; channel < 3; ++channel) {
        if (!plane_bytes[channel] ||
            hp4200_write_verified_plane(io, channel, 1,
                                        plane_bytes[channel], length))
            return -1;
    }
    return 0;
}
