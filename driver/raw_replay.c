#define _POSIX_C_SOURCE 200809L
#include "hp4200_gamma.h"
#include "hp4200_pipeline.h"
#include "hp4200_pixels.h"
#include "hp4200_square_pixels.h"
#include "png_writer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    size_t raw_pixels, output_pixels, line_bytes;
    size_t raw_rows, output_rows, stagger_rows;
    unsigned two_line_mode, sensor_bits, x_dpi, y_dpi;
} capture_header;

static int read_header(FILE *input, capture_header *header)
{
    char line[128], key[32], extra[2];
    unsigned long long value;
    unsigned seen = 0;
    int found_data = 0;
    if (!fgets(line, sizeof(line), input) ||
        strcmp(line, "HP4200RAW 1\n")) return -1;
    memset(header, 0, sizeof(*header));
    header->sensor_bits = 8; /* First captures predate this header field. */
    for (unsigned count = 0; count < 32; ++count) {
        if (!fgets(line, sizeof(line), input)) return -1;
        if (!strcmp(line, "DATA\n")) { found_data = 1; break; }
        if (sscanf(line, "%31s %llu %1s", key, &value, extra) != 2 ||
            value > SIZE_MAX) return -1;
        if ((!strcmp(key, "two_line_mode") || !strcmp(key, "sensor_bits") ||
             !strcmp(key, "x_dpi") || !strcmp(key, "y_dpi")) &&
            value > UINT_MAX) return -1;
#define FIELD(name, bit, target) \
        if (!strcmp(key, name)) { \
            if (seen & (1U << bit)) return -1; \
            seen |= 1U << bit; \
            target = value; \
        } else
        FIELD("raw_pixels", 0, header->raw_pixels)
        FIELD("output_pixels", 1, header->output_pixels)
        FIELD("line_bytes", 2, header->line_bytes)
        FIELD("raw_rows", 3, header->raw_rows)
        FIELD("output_rows", 4, header->output_rows)
        FIELD("stagger_rows", 5, header->stagger_rows)
        FIELD("two_line_mode", 6, header->two_line_mode)
        FIELD("sensor_bits", 7, header->sensor_bits)
        FIELD("x_dpi", 8, header->x_dpi)
        FIELD("y_dpi", 9, header->y_dpi)
        return -1;
#undef FIELD
    }
    if (!(seen & (1U << 4))) header->output_rows = header->raw_rows;
    const unsigned required = (1U << 0) | (1U << 1) | (1U << 2) |
                              (1U << 3) | (1U << 5) | (1U << 6) |
                              (1U << 8) | (1U << 9);
    if (!found_data || (seen & required) != required || !header->raw_pixels ||
        header->raw_pixels > 10000 || !header->output_pixels ||
        header->output_pixels > header->raw_pixels ||
        !header->raw_rows || header->raw_rows > 1000000 ||
        !header->output_rows || header->output_rows > header->raw_rows ||
        !header->stagger_rows || header->stagger_rows % 2 ||
        header->stagger_rows / 2 > 16 || header->two_line_mode > 1 ||
        header->line_bytes != header->raw_pixels * 3 + 1 +
                              header->two_line_mode ||
        (header->sensor_bits != 8 && header->sensor_bits != 16) ||
        !header->x_dpi || !header->y_dpi ||
        header->x_dpi > 600 || header->y_dpi > 1200) return -1;
    return 0;
}

int main(int argc, char **argv)
{
    capture_header header;
    hp4200_color_transform color = {0};
    hp4200_pipeline pipeline = {0};
    hp4200_png_writer png = {0};
    FILE *input = NULL, *output = NULL;
    uint8_t *line = NULL, *rgb = NULL, *display_rgb = NULL, *mask = NULL;
    uint8_t identity[256];
    size_t emitted = 0;
    int status = 1, output_created = 0, pipeline_ready = 0;
    int shift = 0;
    if (argc != 3) {
        fprintf(stderr, "Usage: %s CAPTURE.raw OUTPUT.png\n", argv[0]);
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input || read_header(input, &header)) {
        fputs("Invalid or unreadable HP4200RAW capture.\n", stderr);
        goto done;
    }
    const char *square_setting = getenv("HP4200_SQUARE_PIXELS");
    int square_pixels = square_setting && strcmp(square_setting, "1") == 0;
    if (square_pixels && header.y_dpi != header.x_dpi * 2) {
        fputs("Square-pixel replay requires Y DPI to be twice X DPI.\n", stderr);
        goto done;
    }
    unsigned display_pixels = (unsigned)header.output_pixels *
                              (square_pixels ? 2U : 1U);
    unsigned display_x_dpi = square_pixels ? header.y_dpi : header.x_dpi;
    line = malloc(header.line_bytes);
    rgb = malloc(header.output_pixels * 3);
    if (square_pixels) display_rgb = malloc((size_t)display_pixels * 3);
    mask = malloc(header.raw_rows);
    if (!line || !rgb || !mask || (square_pixels && !display_rgb) ||
        hp4200_sample_mask(mask, header.raw_rows,
                           header.output_rows) != header.output_rows) {
        fputs("Cannot allocate replay rows.\n", stderr);
        goto done;
    }
    hp4200_color_matrix(&color, header.sensor_bits == 16);
    for (unsigned i = 0; i < 256; ++i) identity[i] = (uint8_t)i;
    for (unsigned channel = 0; channel < 3; ++channel)
        hp4200_expand_256_to_1024(identity, color.table[channel]);
    if (hp4200_pipeline_init(&pipeline, header.raw_pixels,
            header.output_pixels, (unsigned)(header.stagger_rows / 2),
            &color)) {
        fputs("Cannot initialize image pipeline.\n", stderr);
        goto done;
    }
    pipeline_ready = 1;
    int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) { perror(argv[2]); goto done; }
    output_created = 1;
    output = fdopen(fd, "wb");
    if (!output) { close(fd); perror("fdopen"); goto done; }
    if (hp4200_png_begin(&png, output, display_pixels,
            (unsigned)header.output_rows, display_x_dpi, header.y_dpi)) {
        fputs("Cannot start replay PNG.\n", stderr);
        goto done;
    }
    for (size_t row = 0; row < header.raw_rows + header.stagger_rows; ++row) {
        size_t got = fread(line, 1, header.line_bytes, input);
        /* At reduced vertical resolution HP can return the final selected
         * output row before reading the last unused raw line. The capture
         * is complete if all output rows have already been emitted. */
        if (!got && feof(input)) break;
        if (got != header.line_bytes) {
            fputs("Raw capture contains an incomplete row.\n", stderr);
            goto done;
        }
        if (row == 0 && header.two_line_mode &&
            line[0] == line[header.line_bytes - 1] &&
            line[header.line_bytes - 2] != line[header.line_bytes - 1])
            shift = 1;
        int ready = hp4200_pipeline_push(&pipeline, line + shift,
            header.raw_pixels * 3, rgb, header.output_pixels * 3);
        if (ready < 0) {
            fputs("Raw row conversion failed.\n", stderr);
            goto done;
        }
        if (ready && row >= header.stagger_rows &&
            mask[row - header.stagger_rows]) {
            if (square_pixels)
                hp4200_expand_horizontal_2x(rgb, header.output_pixels,
                                             display_rgb);
            if (hp4200_png_row(&png,
                               square_pixels ? display_rgb : rgb)) {
                fputs("Replay PNG write failed.\n", stderr);
                goto done;
            }
            ++emitted;
        }
    }
    if (emitted != header.output_rows || fgetc(input) != EOF || ferror(input) ||
        hp4200_png_finish(&png)) {
        fputs("Capture length or PNG output is inconsistent.\n", stderr);
        goto done;
    }
    status = 0;
done:
    hp4200_png_destroy(&png);
    if (pipeline_ready) hp4200_pipeline_destroy(&pipeline);
    free(mask); free(display_rgb); free(rgb); free(line);
    if (output && fclose(output)) { perror("Replay PNG close"); status = 1; }
    if (input) fclose(input);
    if (status && output_created) remove(argv[2]);
    if (!status) fprintf(stderr, "Replayed %zu rows.\n", emitted);
    return status;
}
