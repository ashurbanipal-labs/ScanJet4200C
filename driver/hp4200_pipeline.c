#include "hp4200_pipeline.h"
#include <stdlib.h>
#include <string.h>

int hp4200_pipeline_init(hp4200_pipeline *pipeline,
                          size_t raw_pixels, size_t output_pixels,
                          unsigned row_separation,
                          const hp4200_color_transform *color)
{
    if (!pipeline || !raw_pixels || !output_pixels ||
        output_pixels > raw_pixels || raw_pixels > 10000) return -1;
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->raw_pixels = raw_pixels;
    pipeline->output_pixels = output_pixels;
    pipeline->color = color;
    pipeline->mask = malloc(raw_pixels);
    pipeline->scratch = malloc(raw_pixels * 3);
    if (!pipeline->mask || !pipeline->scratch ||
        hp4200_sample_mask(pipeline->mask, raw_pixels, output_pixels) != output_pixels ||
        hp4200_rows_init(&pipeline->rows, output_pixels, row_separation)) {
        hp4200_pipeline_destroy(pipeline);
        return -1;
    }
    return 0;
}

void hp4200_pipeline_destroy(hp4200_pipeline *pipeline)
{
    if (!pipeline) return;
    hp4200_rows_destroy(&pipeline->rows);
    free(pipeline->mask);
    free(pipeline->scratch);
    memset(pipeline, 0, sizeof(*pipeline));
}

int hp4200_pipeline_push(hp4200_pipeline *pipeline,
                          const uint8_t *raw_rgb, size_t raw_bytes,
                          uint8_t *output_rgb, size_t output_capacity)
{
    int ready;
    if (!pipeline || !pipeline->scratch || !raw_rgb || !output_rgb ||
        raw_bytes < pipeline->raw_pixels * 3 ||
        output_capacity < pipeline->output_pixels * 3) return -1;
    memcpy(pipeline->scratch, raw_rgb, pipeline->raw_pixels * 3);
    if (hp4200_compact_rgb(pipeline->scratch, pipeline->raw_pixels,
                           pipeline->mask) != pipeline->output_pixels * 3)
        return -1;
    ready = hp4200_rows_push(&pipeline->rows, pipeline->scratch, output_rgb);
    if (ready == 1 && pipeline->color)
        hp4200_transform_rgb(output_rgb, pipeline->output_pixels,
                              pipeline->color);
    return ready;
}
