#include "png_writer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t pixels[12] = {
        255, 0, 0, 0, 255, 0,
        0, 0, 255, 200, 150, 100
    };
    uint8_t decoded[12] = {0};
    hp4200_png_writer writer = {0};
    png_image image = {0};
    FILE *file = tmpfile();
    assert(file);
    assert(hp4200_png_begin(&writer, file, 2, 2, 600, 300) == 0);
    assert(hp4200_png_row(&writer, pixels) == 0);
    assert(hp4200_png_row(&writer, pixels + 6) == 0);
    assert(hp4200_png_finish(&writer) == 0);
    hp4200_png_destroy(&writer);
    rewind(file);
    image.version = PNG_IMAGE_VERSION;
    assert(png_image_begin_read_from_stdio(&image, file));
    assert(image.width == 2 && image.height == 2);
    image.format = PNG_FORMAT_RGB;
    assert(png_image_finish_read(&image, NULL, decoded, 0, NULL));
    assert(memcmp(pixels, decoded, sizeof(pixels)) == 0);
    png_image_free(&image);
    rewind(file);
    png_structp reader = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL, NULL, NULL);
    png_infop info = png_create_info_struct(reader);
    png_uint_32 xppm = 0, yppm = 0;
    int unit = -1;
    assert(reader && info);
    if (setjmp(png_jmpbuf(reader))) assert(0);
    png_init_io(reader, file);
    png_read_info(reader, info);
    assert(png_get_pHYs(reader, info, &xppm, &yppm, &unit));
    assert(xppm == 23622 && yppm == 11811 &&
           unit == PNG_RESOLUTION_METER);
    png_destroy_read_struct(&reader, &info, NULL);
    fclose(file);
    puts("HP PNG row export test passed");
    return 0;
}
