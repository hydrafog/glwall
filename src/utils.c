#include "utils.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

 
#define READ_FILE_MAX_SIZE (10 * 1024 * 1024)

char *read_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        LOG_ERROR("File operation failed: unable to open '%s' (errno: %s)", path, strerror(errno));
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    if (length < 0) {
        LOG_ERROR("File operation failed: unable to determine size for '%s' (errno: %s)", path, strerror(errno));
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    if (length > READ_FILE_MAX_SIZE) {
        LOG_ERROR("File operation error: '%s' exceeds maximum allowed size (%d bytes)", path,
                  READ_FILE_MAX_SIZE);
        fclose(file);
        return NULL;
    }

     
    if (length < 0 || (size_t)length + 1 == 0) {
        LOG_ERROR("File operation failed: invalid size for '%s' (errno: %s)", path, strerror(errno));
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)length + 1);
    if (!buffer) {
        LOG_ERROR("Memory allocation failed: insufficient memory for file contents");
        fclose(file);
        return NULL;
    }

    size_t read_length = fread(buffer, 1, length, file);
    if (read_length != (size_t)length) {
        LOG_ERROR("File read operation failed: '%s' (bytes read: %zu, expected: %ld)", path, read_length,
                  length);
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

 
#define MAX_VERTEX_COUNT (1 << 20)

void parse_options(int argc, char *argv[], struct glwall_state *state) {
    LOG_DEBUG(state, "Configuration parsing: processing command-line arguments (%d total)", argc);
    struct option long_options[] = {{"shader", required_argument, 0, 's'},
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
                                    {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "s:dp:m:v:V", long_options, NULL)) != -1) {
        switch (c) {
        case 's':
            state->shader_path = optarg;
            LOG_DEBUG(state, "Configuration: shader path set to '%s'", optarg);
            break;
        case 'd':
            state->debug = true;
            LOG_DEBUG(state, "Configuration: debug mode enabled");
            break;
        case 'p':
            if (strcmp(optarg, "full") == 0) {
                state->power_mode = GLWALL_POWER_MODE_FULL;
                LOG_DEBUG(state, "Configuration: power mode set to full");
            } else if (strcmp(optarg, "throttled") == 0) {
                state->power_mode = GLWALL_POWER_MODE_THROTTLED;
                LOG_DEBUG(state, "Configuration: power mode set to throttled");
            } else if (strcmp(optarg, "paused") == 0) {
                state->power_mode = GLWALL_POWER_MODE_PAUSED;
                LOG_DEBUG(state, "Configuration: power mode set to paused");
            } else {
                LOG_ERROR("Configuration error: invalid power mode '%s' (valid: full|throttled|paused)", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'm':
            if (strcmp(optarg, "none") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_NONE;
                LOG_DEBUG(state, "Configuration: mouse overlay mode set to none");
            } else if (strcmp(optarg, "edge") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_EDGE;
                LOG_DEBUG(state, "Configuration: mouse overlay mode set to edge");
            } else if (strcmp(optarg, "full") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_FULL;
                LOG_DEBUG(state, "Configuration: mouse overlay mode set to full");
            } else {
                LOG_ERROR("Configuration error: invalid mouse overlay mode '%s' (valid: none|edge|full)", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 5: {
            char *endptr;
            long h = strtol(optarg, &endptr, 10);
            if (endptr == optarg) {
                LOG_ERROR("Configuration error: mouse-overlay-height is not a number");
                exit(EXIT_FAILURE);
            }
            if (h <= 0 || h > INT32_MAX) {
                LOG_ERROR("Configuration error: mouse-overlay-height must be between 1 and %d (received: %ld)", INT32_MAX, h);
                exit(EXIT_FAILURE);
            }
            state->mouse_overlay_edge_height = (int32_t)h;
            LOG_DEBUG(state, "Configuration: mouse overlay height set to %ld pixels", h);
            break;
        }
        case 1:
            state->audio_enabled = true;
            LOG_DEBUG(state, "Configuration: audio subsystem enabled");
            break;
        case 2:
            state->audio_enabled = false;
            LOG_DEBUG(state, "Configuration: audio subsystem disabled");
            break;
        case 3:
            if (strcmp(optarg, "pulse") == 0 || strcmp(optarg, "pulseaudio") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_PULSEAUDIO;
                LOG_DEBUG(state, "Configuration: audio source set to PulseAudio");
            } else if (strcmp(optarg, "none") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_NONE;
                LOG_DEBUG(state, "Configuration: audio source set to none");
            } else if (strcmp(optarg, "fake") == 0 || strcmp(optarg, "debug") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_FAKE;
                LOG_DEBUG(state, "Configuration: audio source set to fake (diagnostic mode)");
            } else {
                LOG_ERROR("Configuration error: invalid audio source '%s' (valid: pulse|pulseaudio|fake|debug|none)",
                          optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 6:
            state->audio_device_name = optarg;
            LOG_DEBUG(state, "Configuration: audio device set to '%s'", optarg);
            break;
        case 'v':
            state->vertex_shader_path = optarg;
            state->allow_vertex_shaders = true;
            LOG_DEBUG(state, "Configuration: vertex shader enabled with path '%s'", optarg);
            break;
        case 'V':
            state->allow_vertex_shaders = true;
            LOG_DEBUG(state, "Configuration: vertex shader support enabled");
            break;
        case 4: {
            char *endptr;
            long v = strtol(optarg, &endptr, 10);
            if (endptr == optarg) {
                LOG_ERROR("Configuration error: vertex-count is not a number");
                exit(EXIT_FAILURE);
            }
            if (v <= 0 || v > INT32_MAX || v > MAX_VERTEX_COUNT) {
                LOG_ERROR("Configuration error: vertex-count must be between 1 and %d (received: %ld)", INT32_MAX, v);
                exit(EXIT_FAILURE);
            }
            state->vertex_count = (int32_t)v;
            LOG_DEBUG(state, "Configuration: vertex count set to %ld", v);
            break;
        }
        case 7:
            if (strcmp(optarg, "points") == 0) {
                state->vertex_draw_mode = GL_POINTS;
                LOG_DEBUG(state, "Configuration: vertex draw mode set to points");
            } else if (strcmp(optarg, "lines") == 0) {
                state->vertex_draw_mode = GL_LINES;
                LOG_DEBUG(state, "Configuration: vertex draw mode set to lines");
            } else {
                LOG_ERROR("Configuration error: invalid vertex mode '%s' (valid: points|lines)", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 8:
            state->kernel_input_enabled = true;
            LOG_DEBUG(state, "Configuration: kernel input device monitoring enabled");
            break;
        default:
            fprintf(
                stderr,
                "Usage: %s -s <shader.frag> [--debug] \\\n [--power-mode full|throttled|paused] "
                "\\\n [--mouse-overlay none|edge|full] \\\n [--audio|--no-audio] [--audio-source "
                "pulse|none] \\\n [--audio-device device-name] \\\n [--vertex-shader path "
                "--allow-vertex-shaders] \\\n [--vertex-mode points|lines] \\\n [--kernel-input]\n",
                argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if (!state->shader_path && !state->vertex_shader_path) {
        LOG_ERROR("Configuration error: shader path is required (use -s /path/to/shader.frag)");
        exit(EXIT_FAILURE);
    }
}
