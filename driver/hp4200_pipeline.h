#ifndef HP4200_PIPELINE_H
#define HP4200_PIPELINE_H

#include "hp4200_pixels.h"
#include "hp4200_rows.h"

typedef struct {
    size_t raw_pixels;
    size_t output_pixels;
    uint8_t *mask;
    uint8_t *scratch;
    hp4200_rows rows;
    const hp4200_color_transform *color;
} hp4200_pipeline;

int hp4200_pipeline_init(hp4200_pipeline *pipeline,
                          size_t raw_pixels, size_t output_pixels,
                          unsigned row_separation,
                          const hp4200_color_transform *color);
void hp4200_pipeline_destroy(hp4200_pipeline *pipeline);
/* raw_rgb points after the scan-line status byte(s). Returns 1 when a full
 * output row is produced, 0 while filling color stagger, -1 on error. */
int hp4200_pipeline_push(hp4200_pipeline *pipeline,
                          const uint8_t *raw_rgb, size_t raw_bytes,
                          uint8_t *output_rgb, size_t output_capacity);

#endif
