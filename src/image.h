#pragma once

#include <stdbool.h>
#include <stdint.h>

struct glwall_image {
    int32_t width_px;
    int32_t height_px;

    uint8_t *rgba;
};

bool load_png_rgba8(const char *path, struct glwall_image *out);

void free_glwall_image(struct glwall_image *img);
