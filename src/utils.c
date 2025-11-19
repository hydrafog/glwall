/**
 * @file utils.c
 * @brief Implementation of utility functions for GLWall
 * 
 * This file implements file I/O and command-line parsing utilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include "utils.h"

// Public Function Implementations

/**
 * @brief Reads entire file contents into memory
 * 
 * Opens the file, determines its size, allocates a buffer, reads the
 * contents, and returns the null-terminated buffer.
 * 
 * @param path Path to the file to read
 * @return Pointer to allocated buffer, or NULL on error
 */
char *read_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        LOG_ERROR("Failed to open file '%s': %s", path, strerror(errno));
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length < 0) {
        LOG_ERROR("Failed to determine file size for '%s': %s", path, strerror(errno));
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        LOG_ERROR("Failed to allocate memory for file contents");
        fclose(file);
        return NULL;
    }

    // Check the return value of fread to ensure complete read.
    size_t read_length = fread(buffer, 1, length, file);
    if (read_length != (size_t)length) {
        LOG_ERROR("Failed to read full file '%s'. Read %zu bytes, expected %ld.", path, read_length, length);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

/**
 * @brief Parses command-line options
 * 
 * Processes -s/--shader, -d/--debug and extended options for power mode,
 * mouse overlay, audio, and vertex shaders. Exits with error message
 * if required shader path is not provided.
 * 
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param state Pointer to state structure to populate
 */
void parse_options(int argc, char *argv[], struct glwall_state *state) {
    struct option long_options[] = {
        {"shader", required_argument, 0, 's'},
        {"debug", no_argument, 0, 'd'},
        {"power-mode", required_argument, 0, 'p'},
        {"mouse-overlay", required_argument, 0, 'm'},
        {"mouse-overlay-height", required_argument, 0, 5},
        {"audio", no_argument, 0, 1},
        {"no-audio", no_argument, 0, 2},
        {"audio-source", required_argument, 0, 3},
        {"audio-device", required_argument, 0, 6},
        {"vertex-shader", required_argument, 0, 'v'},
        {"allow-vertex-shaders", no_argument, 0, 'V'},
        {"vertex-count", required_argument, 0, 4},
        {"vertex-mode", required_argument, 0, 7},
        {"kernel-input", no_argument, 0, 8},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "s:dp:m:v:V", long_options, NULL)) != -1) {
        switch (c) {
            case 's':
                state->shader_path = optarg;
                break;
            case 'd':
                state->debug = true;
                break;
            case 'p':
                if (strcmp(optarg, "full") == 0) {
                    state->power_mode = GLWALL_POWER_MODE_FULL;
                } else if (strcmp(optarg, "throttled") == 0) {
                    state->power_mode = GLWALL_POWER_MODE_THROTTLED;
                } else if (strcmp(optarg, "paused") == 0) {
                    state->power_mode = GLWALL_POWER_MODE_PAUSED;
                } else {
                    LOG_ERROR("Invalid power mode '%s'. Expected full|throttled|paused.", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'm':
                if (strcmp(optarg, "none") == 0) {
                    state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_NONE;
                } else if (strcmp(optarg, "edge") == 0) {
                    state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_EDGE;
                } else if (strcmp(optarg, "full") == 0) {
                    state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_FULL;
                } else {
                    LOG_ERROR("Invalid mouse overlay mode '%s'. Expected none|edge|full.", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 5: // --mouse-overlay-height
            {
                long h = strtol(optarg, NULL, 10);
                if (h <= 0) {
                    LOG_ERROR("mouse-overlay-height must be positive, got %ld", h);
                    exit(EXIT_FAILURE);
                }
                state->mouse_overlay_edge_height = (int32_t)h;
                break;
            }
            case 1: // --audio
                state->audio_enabled = true;
                break;
            case 2: // --no-audio
                state->audio_enabled = false;
                break;
            case 3: // --audio-source
                if (strcmp(optarg, "pulse") == 0 || strcmp(optarg, "pulseaudio") == 0) {
                    state->audio_source = GLWALL_AUDIO_SOURCE_PULSEAUDIO;
                } else if (strcmp(optarg, "none") == 0) {
                    state->audio_source = GLWALL_AUDIO_SOURCE_NONE;
                } else {
                    LOG_ERROR("Invalid audio source '%s'. Expected pulse|pulseaudio|none.", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 6: // --audio-device
                state->audio_device_name = optarg;
                break;
            case 'v':
                state->vertex_shader_path = optarg;
                state->allow_vertex_shaders = true;
                break;
            case 'V':
                state->allow_vertex_shaders = true;
                break;
            case 4: // --vertex-count
            {
                long v = strtol(optarg, NULL, 10);
                if (v <= 0) {
                    LOG_ERROR("vertex-count must be positive, got %ld", v);
                    exit(EXIT_FAILURE);
                }
                state->vertex_count = (int32_t)v;
                break;
            }
            case 7: // --vertex-mode
                if (strcmp(optarg, "points") == 0) {
                    state->vertex_draw_mode = GL_POINTS;
                } else if (strcmp(optarg, "lines") == 0) {
                    state->vertex_draw_mode = GL_LINES;
                } else {
                    LOG_ERROR("Invalid vertex mode '%s'. Expected points|lines.", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 8: // --kernel-input
                state->kernel_input_enabled = true;
                break;
            default:
                fprintf(stderr,
                        "Usage: %s -s <shader.frag> [--debug] \\\n [--power-mode full|throttled|paused] \\\n [--mouse-overlay none|edge|full] \\\n [--audio|--no-audio] [--audio-source pulse|none] \\\n [--audio-device device-name] \\\n [--vertex-shader path --allow-vertex-shaders] \\\n [--vertex-mode points|lines] \\\n [--kernel-input]\n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (!state->shader_path) {
        LOG_ERROR("Shader path is required. Use -s /path/to/shader.frag");
        exit(EXIT_FAILURE);
    }
}
