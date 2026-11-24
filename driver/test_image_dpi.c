#define _POSIX_C_SOURCE 200809L
#include "image_dpi.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void write_fixture(const char *path, const uint8_t *bytes, size_t length)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void read_fixture(const char *path, uint8_t *bytes, size_t length)
{
    FILE *file = fopen(path, "rb");
    assert(file && fread(bytes, 1, length, file) == length);
    assert(fclose(file) == 0);
}

int main(void)
{
    char path[] = "/tmp/hp4200-dpi-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0);
    uint8_t jpeg[20] = {0xff, 0xd8, 0xff, 0xe0, 0, 16,
                        'J', 'F', 'I', 'F', 0, 1, 1, 0,
                        0, 72, 0, 72, 0, 0};
    write_fixture(path, jpeg, sizeof(jpeg));
    assert(hp4200_set_jpeg_xy_dpi(path, 600, 1200) == 0);
    read_fixture(path, jpeg, sizeof(jpeg));
    assert(jpeg[13] == 1 && jpeg[14] == 2 && jpeg[15] == 88 &&
           jpeg[16] == 4 && jpeg[17] == 176);
    uint8_t prefixed_jpeg[26] = {
        0xff, 0xd8, 0xff, 0xe1, 0, 4, 'x', 'y'
    };
    for (unsigned i = 0; i < 18; ++i)
        prefixed_jpeg[8 + i] = jpeg[2 + i];
    write_fixture(path, prefixed_jpeg, sizeof(prefixed_jpeg));
    assert(hp4200_set_jpeg_xy_dpi(path, 300, 600) == 0);
    read_fixture(path, prefixed_jpeg, sizeof(prefixed_jpeg));
    assert(prefixed_jpeg[19] == 1 && prefixed_jpeg[20] == 1 &&
           prefixed_jpeg[21] == 44 && prefixed_jpeg[22] == 2 &&
           prefixed_jpeg[23] == 88);

    uint8_t tiff[66] = {
        'I', 'I', 42, 0, 8, 0, 0, 0, 3, 0,
        0x1a, 1, 5, 0, 1, 0, 0, 0, 50, 0, 0, 0,
        0x1b, 1, 5, 0, 1, 0, 0, 0, 58, 0, 0, 0,
        0x28, 1, 3, 0, 1, 0, 0, 0, 2, 0, 0, 0
    };
    write_fixture(path, tiff, sizeof(tiff));
    assert(hp4200_set_tiff_xy_dpi(path, 600, 1200) == 0);
    read_fixture(path, tiff, sizeof(tiff));
    assert(tiff[50] == 88 && tiff[51] == 2 && tiff[54] == 1 &&
           tiff[58] == 176 && tiff[59] == 4 && tiff[62] == 1);
    uint8_t big_endian_tiff[66] = {
        'M', 'M', 0, 42, 0, 0, 0, 8, 0, 3,
        1, 0x1a, 0, 5, 0, 0, 0, 1, 0, 0, 0, 50,
        1, 0x1b, 0, 5, 0, 0, 0, 1, 0, 0, 0, 58,
        1, 0x28, 0, 3, 0, 0, 0, 1, 0, 0, 0, 0
    };
    write_fixture(path, big_endian_tiff, sizeof(big_endian_tiff));
    assert(hp4200_set_tiff_xy_dpi(path, 600, 1200) == 0);
    read_fixture(path, big_endian_tiff, sizeof(big_endian_tiff));
    assert(big_endian_tiff[42] == 0 && big_endian_tiff[43] == 2 &&
           big_endian_tiff[50] == 0 && big_endian_tiff[51] == 0 &&
           big_endian_tiff[52] == 2 && big_endian_tiff[53] == 88 &&
           big_endian_tiff[57] == 1 && big_endian_tiff[60] == 4 &&
           big_endian_tiff[61] == 176 && big_endian_tiff[65] == 1);
    assert(unlink(path) == 0);
    puts("HP asymmetric image DPI export tests passed");
    return 0;
}
