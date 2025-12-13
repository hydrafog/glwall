#define _POSIX_C_SOURCE 200809L

#include "image.h"
#include "utils.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void png_read_abort(png_structp png_ptr, png_infop info_ptr, FILE *fp, png_bytep *row_ptrs,
                           uint8_t *rgba) {
    if (row_ptrs)
        free(row_ptrs);
    if (rgba)
        free(rgba);
    if (fp)
        fclose(fp);
    if (png_ptr && info_ptr)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    else if (png_ptr)
        png_destroy_read_struct(&png_ptr, NULL, NULL);
}

bool load_png_rgba8(const char *path, struct glwall_image *out) {
    if (!path || !out)
        return false;

    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOG_ERROR("File operation failed: unable to open '%s'", path);
        return false;
    }

    uint8_t sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) {
        LOG_ERROR("PNG decode failed: invalid signature '%s'", path);
        fclose(fp);
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        LOG_ERROR("PNG decode failed: libpng error '%s'", path);
        png_read_abort(png_ptr, info_ptr, fp, NULL, NULL);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    png_read_info(png_ptr, info_ptr);

    png_uint_32 width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

    if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return false;
    }

    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    if (!(color_type & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);

    png_size_t rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    if (rowbytes != width * 4) {
    }

    uint8_t *rgba = malloc((size_t)rowbytes * (size_t)height);
    if (!rgba) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return false;
    }

    png_bytep *row_ptrs = malloc(sizeof(png_bytep) * (size_t)height);
    if (!row_ptrs) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        free(rgba);
        fclose(fp);
        return false;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        row_ptrs[y] = (png_bytep)(rgba + (size_t)y * (size_t)rowbytes);
    }

    png_read_image(png_ptr, row_ptrs);
    png_read_end(png_ptr, NULL);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    free(row_ptrs);

    out->width_px = (int32_t)width;
    out->height_px = (int32_t)height;
    out->rgba = rgba;
    return true;
}

void free_glwall_image(struct glwall_image *img) {
    if (!img)
        return;
    if (img->rgba)
        free(img->rgba);
    img->rgba = NULL;
    img->width_px = 0;
    img->height_px = 0;
}
