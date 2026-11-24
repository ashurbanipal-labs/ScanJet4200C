#include "png_writer.h"

#include <limits.h>
#include <string.h>

int hp4200_png_begin(hp4200_png_writer *writer, FILE *file,
                      unsigned width, unsigned height,
                      unsigned x_dpi, unsigned y_dpi)
{
    if (!writer || !file || !width || !height ||
        width > INT_MAX / 3 || height > INT_MAX ||
        !x_dpi || !y_dpi || x_dpi > 9600 || y_dpi > 9600) return -1;
    memset(writer, 0, sizeof(*writer));
    writer->file = file;
    writer->width = width;
    writer->height = height;
    writer->png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                           NULL, NULL);
    if (!writer->png) return -1;
    writer->info = png_create_info_struct(writer->png);
    if (!writer->info) return -1;
    if (setjmp(png_jmpbuf(writer->png))) return -1;
    png_init_io(writer->png, file);
    png_set_IHDR(writer->png, writer->info, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_pHYs(writer->png, writer->info,
                  (png_uint_32)(((uint64_t)x_dpi * 10000 + 127) / 254),
                  (png_uint_32)(((uint64_t)y_dpi * 10000 + 127) / 254),
                  PNG_RESOLUTION_METER);
    png_write_info(writer->png, writer->info);
    return 0;
}

int hp4200_png_row(hp4200_png_writer *writer, const uint8_t *rgb)
{
    if (!writer || !writer->png || !rgb || writer->rows >= writer->height)
        return -1;
    if (setjmp(png_jmpbuf(writer->png))) return -1;
    png_write_row(writer->png, (png_const_bytep)rgb);
    ++writer->rows;
    return 0;
}

int hp4200_png_finish(hp4200_png_writer *writer)
{
    if (!writer || !writer->png || writer->rows != writer->height)
        return -1;
    if (setjmp(png_jmpbuf(writer->png))) return -1;
    png_write_end(writer->png, writer->info);
    return fflush(writer->file) == 0 ? 0 : -1;
}

void hp4200_png_destroy(hp4200_png_writer *writer)
{
    if (!writer) return;
    if (writer->png)
        png_destroy_write_struct(&writer->png, &writer->info);
    memset(writer, 0, sizeof(*writer));
}
