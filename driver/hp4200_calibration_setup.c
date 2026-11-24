#include "hp4200_calibration_setup.h"

#include <string.h>

int hp4200_write_color_calibration_setup(const hp4200_io *io,
                                         const hp4200_register_bank *bank,
                                         unsigned origin_x,
                                         unsigned horizontal_dpi)
{
    hp4200_register_bank calibration;
    unsigned end, threshold;
    if (!io || !io->write_register || !bank || origin_x > 0xffff - 0x13ec ||
        horizontal_dpi == 0 || horizontal_dpi > 1200) return -1;
    memcpy(&calibration, bank, sizeof(calibration));
    end = origin_x + 0x13ec;
    /* FUN_10006cb0 operates on the register table built by FUN_10006b10. */
    calibration.value[0x09] = (uint8_t)(bank->value[0x09] | 0x38);
    calibration.value[0x22] = (uint8_t)(origin_x >> 8);
    calibration.value[0x23] = (uint8_t)origin_x;
    calibration.value[0x24] = (uint8_t)(end >> 8);
    calibration.value[0x25] = (uint8_t)end;
    calibration.value[0x38] = 0;
    calibration.value[0x39] = 0;
    calibration.value[0x3a] = 0;
    calibration.value[0x3b] = 1;
    calibration.value[0x3c] = 1;
    calibration.value[0x3d] = 1;
    calibration.value[0x44] = 0;
    calibration.value[0x48] = 0;
    calibration.value[0x49] = 0;
    calibration.value[0x4a] = 0;
    calibration.value[0x4b] = 0;
    /* Color mode DAT_1000f024 == 4 selects factor 0x1fe. */
    threshold = (((horizontal_dpi * 0x1feU) / 10U) >> 8) >> 2;
    calibration.value[0x4e] = (uint8_t)(threshold + 2);
    calibration.value[0x4f] = 2;
    calibration.value[0x50] = 0;
    calibration.value[0x52] = 0;
    calibration.value[0x53] = 0;
    calibration.value[0x5a] = 0;
    return hp4200_write_scan_table(io, &calibration);
}

int hp4200_write_offset_setup(const hp4200_io *io,
                               const hp4200_register_bank *bank,
                               const uint8_t gain[3])
{
    static const uint8_t flagged[] = {
        0x09, 0x0a, 0x1e, 0x1f, 0x22, 0x23, 0x24, 0x25,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x46, 0x47, 0x4e,
    };
    hp4200_register_bank offset;
    if (!io || !io->write_register || !bank || !gain) return -1;
    memcpy(&offset, bank, sizeof(offset));
    offset.value[0x09] = 0x38;
    offset.value[0x0a] = 0;
    offset.value[0x1e] = 0;
    offset.value[0x1f] = 0x22;
    /* FUN_100072e0 centers the fixed 0x4b0-unit black reference strip. */
    offset.value[0x22] = 0;
    offset.value[0x23] = 0x27;
    offset.value[0x24] = 0x04;
    offset.value[0x25] = 0xd7;
    for (unsigned channel = 0; channel < 3; ++channel) {
        offset.value[0x38 + channel] = 0;
        offset.value[0x3b + channel] = gain[channel];
    }
    offset.value[0x4e] = 9;
    for (size_t i = 0; i < sizeof(flagged); ++i)
        if (io->write_register(io->context, flagged[i],
                               offset.value[flagged[i]])) return -1;
    return 0;
}

int hp4200_write_shading_setup(const hp4200_io *io,
                                const hp4200_register_bank *bank,
                                unsigned origin_x, const uint8_t gain[3],
                                const uint8_t offset[3])
{
    hp4200_register_bank shading;
    unsigned end;
    if (!io || !io->write_register || !bank || !gain || !offset ||
        origin_x > 0xffff - 0x13ec) return -1;
    memcpy(&shading, bank, sizeof(shading));
    end = origin_x + 0x13ec;
    shading.value[0x09] = 0x38;
    shading.value[0x0a] = 0;
    shading.value[0x1e] = bank->value[0x1e];
    shading.value[0x1f] = bank->value[0x1f];
    shading.value[0x22] = (uint8_t)(origin_x >> 8);
    shading.value[0x23] = (uint8_t)origin_x;
    shading.value[0x24] = (uint8_t)(end >> 8);
    shading.value[0x25] = (uint8_t)end;
    for (unsigned channel = 0; channel < 3; ++channel) {
        shading.value[0x38 + channel] = offset[channel];
        shading.value[0x3b + channel] = gain[channel];
    }
    shading.value[0x3e] = 0;
    shading.value[0x44] = 0;
    /* FUN_100060c0(75) with HP's 5409-clock color calibration table. */
    shading.value[0x46] = 0x01;
    shading.value[0x47] = 0x52;
    shading.value[0x48] = 0;
    shading.value[0x49] = 0;
    shading.value[0x4a] = 0;
    shading.value[0x4b] = 0;
    shading.value[0x4e] = 30;
    shading.value[0x4f] = 2;
    shading.value[0x50] = 0;
    shading.value[0x52] = 0;
    shading.value[0x53] = 0;
    shading.value[0x5a] = 0;
    return hp4200_write_scan_table(io, &shading);
}
