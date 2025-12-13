#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_FILE_MAX_SIZE (10 * 1024 * 1024)

char *read_file(const char *path) {
    assert(path != NULL);

    FILE *file = fopen(path, "rb");
    if (!file) {
        LOG_ERROR("File operation failed: unable to open '%s' (errno: %s)", path, strerror(errno));
        return NULL;
    }

    long length = -1;
#if defined(_POSIX_VERSION)
    struct stat st;
    if (fileno(file) >= 0 && fstat(fileno(file), &st) == 0 && S_ISREG(st.st_mode)) {
        if (st.st_size > READ_FILE_MAX_SIZE) {
            LOG_ERROR("File operation error: '%s' exceeds maximum allowed size (%d bytes)", path,
                      READ_FILE_MAX_SIZE);
            fclose(file);
            return NULL;
        }
        length = (long)st.st_size;
    } else {
        if (fseek(file, 0, SEEK_END) == 0) {
            long t = ftell(file);
            if (t >= 0)
                length = t;
            fseek(file, 0, SEEK_SET);
        }
    }
#else
    if (fseek(file, 0, SEEK_END) == 0) {
        long t = ftell(file);
        if (t >= 0)
            length = t;
        fseek(file, 0, SEEK_SET);
    }
#endif

    if (length < 0) {
        LOG_ERROR("File operation failed: unable to determine size for '%s' (errno: %s)", path,
                  strerror(errno));
        fclose(file);
        return NULL;
    }

    if (length > READ_FILE_MAX_SIZE) {
        LOG_ERROR("File operation error: '%s' exceeds maximum allowed size (%d bytes)", path,
                  READ_FILE_MAX_SIZE);
        fclose(file);
        return NULL;
    }

    size_t buf_size = (size_t)length + 1;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        LOG_ERROR("%s", "Memory allocation failed: insufficient memory for file contents");
        fclose(file);
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < (size_t)length) {
        size_t r = fread(buffer + total_read, 1, (size_t)length - total_read, file);
        if (r == 0) {
            if (feof(file))
                break;
            if (ferror(file)) {
                LOG_ERROR("File read operation failed: '%s' (errno: %s)", path, strerror(errno));
                free(buffer);
                fclose(file);
                return NULL;
            }
        }
        total_read += r;
    }

    buffer[total_read] = '\0';
    fclose(file);
    return buffer;
}

#define MAX_VERTEX_COUNT (1 << 20)

void parse_options(int argc, char *argv[], struct glwall_state *state) {
    assert(argv != NULL);
    assert(state != NULL);

    LOG_DEBUG(state, "Configuration parsing: processing command-line arguments (%d total)", argc);
    struct option long_options[] = {{"shader", required_argument, 0, 's'},
                                    {"image", required_argument, 0, 'i'},
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
                                    {"layer", required_argument, 0, 9},
                                    {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "s:i:dp:m:v:V", long_options, NULL)) != -1) {
        switch (c) {
        case 's':
            state->shader_path = optarg;
            LOG_DEBUG(state, "Configuration: shader path set to '%s'", optarg);
            break;
        case 'i':
            state->image_path = optarg;
            LOG_DEBUG(state, "Configuration: image path set to '%s'", optarg);
            break;
        case 'd':
            state->debug = true;
            LOG_DEBUG(state, "%s", "Configuration: debug mode enabled");
            break;
        case 'p':
            if (strcmp(optarg, "full") == 0) {
                state->power_mode = GLWALL_POWER_MODE_FULL;
                LOG_DEBUG(state, "%s", "Configuration: power mode set to full");
            } else if (strcmp(optarg, "throttled") == 0) {
                state->power_mode = GLWALL_POWER_MODE_THROTTLED;
                LOG_DEBUG(state, "%s", "Configuration: power mode set to throttled");
            } else if (strcmp(optarg, "paused") == 0) {
                state->power_mode = GLWALL_POWER_MODE_PAUSED;
                LOG_DEBUG(state, "%s", "Configuration: power mode set to paused");
            } else {
                LOG_ERROR(
                    "Configuration error: invalid power mode '%s' (valid: full|throttled|paused)",
                    optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'm':
            if (strcmp(optarg, "none") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_NONE;
                LOG_DEBUG(state, "%s", "Configuration: mouse overlay mode set to none");
            } else if (strcmp(optarg, "edge") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_EDGE;
                LOG_DEBUG(state, "%s", "Configuration: mouse overlay mode set to edge");
            } else if (strcmp(optarg, "full") == 0) {
                state->mouse_overlay_mode = GLWALL_MOUSE_OVERLAY_FULL;
                LOG_DEBUG(state, "%s", "Configuration: mouse overlay mode set to full");
            } else {
                LOG_ERROR(
                    "Configuration error: invalid mouse overlay mode '%s' (valid: none|edge|full)",
                    optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 5: {
            char *endptr;
            long h = strtol(optarg, &endptr, 10);
            if (endptr == optarg) {
                LOG_ERROR("%s", "Configuration error: mouse-overlay-height is not a number");
                exit(EXIT_FAILURE);
            }
            if (h <= 0 || h > INT32_MAX) {
                LOG_ERROR("Configuration error: mouse-overlay-height must be between 1 and %d "
                          "(received: %ld)",
                          INT32_MAX, h);
                exit(EXIT_FAILURE);
            }
            state->mouse_overlay_edge_height_px = (int32_t)h;
            LOG_DEBUG(state, "Configuration: mouse overlay height set to %ld pixels", h);
            break;
        }
        case 1:
            state->audio_enabled = true;
            LOG_DEBUG(state, "%s", "Configuration: audio subsystem enabled");
            break;
        case 2:
            state->audio_enabled = false;
            LOG_DEBUG(state, "%s", "Configuration: audio subsystem disabled");
            break;
        case 3:
            if (strcmp(optarg, "pulse") == 0 || strcmp(optarg, "pulseaudio") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_PULSEAUDIO;
                LOG_DEBUG(state, "%s", "Configuration: audio source set to PulseAudio");
            } else if (strcmp(optarg, "none") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_NONE;
                LOG_DEBUG(state, "%s", "Configuration: audio source set to none");
            } else if (strcmp(optarg, "fake") == 0 || strcmp(optarg, "debug") == 0) {
                state->audio_source = GLWALL_AUDIO_SOURCE_FAKE;
                LOG_DEBUG(state, "%s", "Configuration: audio source set to fake (diagnostic mode)");
            } else {
                LOG_ERROR("Configuration error: invalid audio source '%s' (valid: "
                          "pulse|pulseaudio|fake|debug|none)",
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
            LOG_DEBUG(state, "%s", "Configuration: vertex shader support enabled");
            break;
        case 4: {
            char *endptr;
            long v = strtol(optarg, &endptr, 10);
            if (endptr == optarg) {
                LOG_ERROR("%s", "Configuration error: vertex-count is not a number");
                exit(EXIT_FAILURE);
            }
            if (v <= 0 || v > INT32_MAX || v > MAX_VERTEX_COUNT) {
                LOG_ERROR(
                    "Configuration error: vertex-count must be between 1 and %d (received: %ld)",
                    INT32_MAX, v);
                exit(EXIT_FAILURE);
            }
            state->vertex_count = (int32_t)v;
            LOG_DEBUG(state, "Configuration: vertex count set to %ld", v);
            break;
        }
        case 7:
            if (strcmp(optarg, "points") == 0) {
                state->vertex_draw_mode = GL_POINTS;
                LOG_DEBUG(state, "%s", "Configuration: vertex draw mode set to points");
            } else if (strcmp(optarg, "lines") == 0) {
                state->vertex_draw_mode = GL_LINES;
                LOG_DEBUG(state, "%s", "Configuration: vertex draw mode set to lines");
            } else {
                LOG_ERROR("Configuration error: invalid vertex mode '%s' (valid: points|lines)",
                          optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 8:
            state->kernel_input_enabled = true;
            LOG_DEBUG(state, "%s", "Configuration: kernel input device monitoring enabled");
            break;
        case 9:
            if (strcmp(optarg, "background") == 0) {
                state->layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
            } else if (strcmp(optarg, "bottom") == 0) {
                state->layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
            } else if (strcmp(optarg, "top") == 0) {
                state->layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
            } else if (strcmp(optarg, "overlay") == 0) {
                state->layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
            } else {
                LOG_ERROR("Configuration error: invalid layer '%s' (valid: "
                          "background|bottom|top|overlay)",
                          optarg);
                exit(EXIT_FAILURE);
            }
            LOG_DEBUG(state, "Configuration: layer set to '%s'", optarg);
            break;
        default:
            fprintf(
                stderr,
                "Usage: %s -s <shader.frag|preset.slangp|preset.glslp> [--image path.png] "
                "[--debug] \\\n+ [--power-mode full|throttled|paused] "
                "\\\n [--mouse-overlay none|edge|full] \\\n [--audio|--no-audio] [--audio-source "
                "pulse|none] \\\n [--audio-device device-name] \\\n [--vertex-shader path "
                "--allow-vertex-shaders] \\\n [--vertex-mode points|lines] \\\n [--kernel-input] "
                "\\\n [--layer background|bottom|top|overlay]\n",
                argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if (!state->shader_path && !state->vertex_shader_path) {
        LOG_ERROR("%s",
                  "Configuration error: shader path is required (use -s /path/to/shader.frag)");
        exit(EXIT_FAILURE);
    }
}
