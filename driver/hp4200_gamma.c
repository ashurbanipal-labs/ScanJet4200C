#include "hp4200_gamma.h"
#include "hp4200_ram.h"

void hp4200_expand_256_to_1024(const uint8_t input[256],
                                uint8_t output[1024])
{
    if (!input || !output) return;
    for (unsigned i = 0; i < 255; ++i) {
        int difference = (int)input[i + 1] - input[i];
        for (unsigned fraction = 0; fraction < 4; ++fraction)
            output[i * 4 + fraction] =
                (uint8_t)((int)input[i] + difference * (int)fraction / 4);
    }
    for (unsigned fraction = 0; fraction < 4; ++fraction)
        output[1020 + fraction] = input[255];
}

int hp4200_build_asic_table(uint8_t output[1024], unsigned channel,
                             unsigned black_reference, int special_sensor_mode)
{
    uint8_t correction[256];
    int white = channel == 2 ? 0xe3 : 0xe6;
    if (!output || channel > 2 || black_reference >= (unsigned)white) return -1;
    for (unsigned i = 0; i < 256; ++i) {
        int scale = (int)i * 0xdc;
        int value = (scale - white * 0xdc) /
                    (white - (int)black_reference) + 0xe6;
        if (value < 0) value = 0;
        if (value > 255) value = 255;
        if (scale >= 0xc5a9 && scale <= 55000) {
            if (scale < 0xc9f5 || scale > 0xd28c) value -= 1;
            else value -= 2;
        }
        correction[i] = (uint8_t)value;
        if (special_sensor_mode) {
            correction[i] = (uint8_t)i;
            if (channel == 2) {
                unsigned blue = i * 0xeb / 0xe6;
                correction[i] = (uint8_t)(blue > 255 ? 255 : blue);
            }
        }
    }
    hp4200_expand_256_to_1024(correction, output);
    return 0;
}

int hp4200_upload_color_table(const hp4200_io *io, unsigned channel,
                               const uint8_t table[1024])
{
    return hp4200_write_verified_plane(io, channel, 0, table, 1024);
}
