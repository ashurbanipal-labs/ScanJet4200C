#ifndef HP4200_PNG_WRITER_H
#define HP4200_PNG_WRITER_H

#include <png.h>
#include <stdio.h>
#include <stdint.h>

typedef struct {
    png_structp png;
    png_infop info;
    FILE *file;
    unsigned width, height;
    unsigned rows;
} hp4200_png_writer;

int hp4200_png_begin(hp4200_png_writer *writer, FILE *file,
                      unsigned width, unsigned height,
                      unsigned x_dpi, unsigned y_dpi);
int hp4200_png_row(hp4200_png_writer *writer, const uint8_t *rgb);
int hp4200_png_finish(hp4200_png_writer *writer);
void hp4200_png_destroy(hp4200_png_writer *writer);

#endif
