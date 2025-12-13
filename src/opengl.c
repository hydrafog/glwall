#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <time.h>

#include "audio.h"
#include "image.h"
#include "input.h"
#include "opengl.h"
#include "pipeline.h"
#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <unistd.h>

/* Request flag set by the async signal handler; checked on the main thread. */
static volatile sig_atomic_t glwall_dump_gpu_flag = 0;

static void glwall_profile_signal_handler(int sig) {
    (void)sig;
    glwall_dump_gpu_flag = 1;
}

static const char *vertex_shader_src = "#version 330 core\n"
                                       "const vec2 verts[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, "
                                       "-1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));\n"
                                       "void main() {\n"
                                       "    gl_Position = vec4(verts[gl_VertexID], 0.0, 1.0);\n"
                                       "}\n";

static const char *vertex_preamble = "#version 330 core\n"
                                     "#define vertexId float(gl_VertexID)\n"
                                     "uniform float vertexCount;\n"
                                     "uniform sampler2D sound;\n"
                                     "out vec4 v_color;\n";

static const char *fragment_preamble = "#version 330 core\n"
                                       "layout(std140, binding = 0) uniform glwall_state_block {\n"
                                       "  vec4 iResolution;\n"
                                       "  vec4 iTime_frame; /* x=iTime, y=iTimeDelta, z=iFrame */\n"
                                       "  vec4 iMouse;\n"
                                       "};\n"
                                       "#define gl_FragColor fragColor\n"
                                       "out vec4 fragColor;\n"
                                       "in vec4 v_color;\n";

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source);
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src,
                                    const char *frag_src);
static char *concat_preamble(const char *preamble, const char *source);

static bool is_preset_path(const char *path) {
    if (!path)
        return false;
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    return strcmp(dot, ".slangp") == 0 || strcmp(dot, ".glslp") == 0;
}

static char *strip_version_directive(const char *source) {

    const char *line_start = source;
    const char *version_line = NULL;
    const char *version_end = NULL;

    while (*line_start) {

        const char *content_start = line_start;
        while (*content_start && (*content_start == ' ' || *content_start == '\t')) {
            content_start++;
        }

        if (strncmp(content_start, "#version", 8) == 0) {
            version_line = line_start;

            version_end = strchr(content_start, '\n');
            if (version_end) {
                version_end++;
            } else {
                version_end = content_start + strlen(content_start);
            }
            break;
        }

        const char *next_line = strchr(line_start, '\n');
        if (next_line) {
            line_start = next_line + 1;
        } else {
            break;
        }
    }

    if (!version_line) {
        return strdup(source);
    }

    size_t before_len = version_line - source;
    size_t after_len = strlen(version_end);

    if (before_len > SIZE_MAX - after_len - 1) {
        LOG_ERROR("%s", "Memory allocation overflow prevented in strip_version_directive");
        return NULL;
    }
    char *result = malloc(before_len + after_len + 1);
    if (!result)
        return NULL;

    memcpy(result, source, before_len);
    memcpy(result + before_len, version_end, after_len);
    result[before_len + after_len] = '\0';

    return result;
}

static char *concat_preamble(const char *preamble, const char *source) {
    size_t plen = strlen(preamble);
    size_t slen = strlen(source);

    if (plen > SIZE_MAX - slen - 1) {
        LOG_ERROR("%s", "Memory allocation overflow prevented in concat_preamble");
        return NULL;
    }
    char *result = malloc(plen + slen + 1);
    if (!result)
        return NULL;
    memcpy(result, preamble, plen);
    memcpy(result + plen, source, slen);
    result[plen + slen] = '\0';
    return result;
}

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source) {
    LOG_DEBUG(state, "OpenGL subsystem: shader compilation initiated (type: %u)", type);
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            char *info_log = malloc(log_len);
            if (info_log) {
                glGetShaderInfoLog(shader, log_len, NULL, info_log);
                LOG_ERROR("OpenGL subsystem error: shader compilation failed (details: %s)",
                          info_log);
                free(info_log);
            } else {
                LOG_ERROR(
                    "%s",
                    "OpenGL subsystem error: shader compilation failed (memory allocation failed)");
            }
        } else {
            LOG_ERROR("%s", "OpenGL subsystem error: shader compilation failed (no log available)");
        }
        glDeleteShader(shader);
        return 0;
    }
    LOG_DEBUG(state, "%s", "OpenGL subsystem: shader compilation completed successfully");
    return shader;
}

static GLuint create_shader_program(struct glwall_state *state, const char *vert_src,
                                    const char *frag_src) {
    LOG_DEBUG(state, "%s", "OpenGL subsystem: shader program creation initiated");
    GLuint vert = compile_shader(state, GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(state, GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag)
        return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char *log = malloc(log_len);
        glGetProgramInfoLog(program, log_len, NULL, log);
        LOG_ERROR("OpenGL subsystem error: shader program linking failed (details: %s)", log);
        free(log);
        glDeleteProgram(program);
        return 0;
    }
    LOG_DEBUG(state, "%s", "OpenGL subsystem: shader program creation completed successfully");
    return program;
}

bool init_opengl(struct glwall_state *state) {
    assert(state != NULL);
    assert(state->outputs != NULL);

    LOG_DEBUG(state, "%s", "OpenGL subsystem initialization commenced");
    struct glwall_output *first_output = state->outputs;
    if (!first_output)
        return false;

    if (!eglMakeCurrent(state->egl_display, first_output->egl_surface, first_output->egl_surface,
                        state->egl_context)) {
        LOG_ERROR("%s", "EGL subsystem error: unable to set current EGL context");
        return false;
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        LOG_ERROR("%s", "OpenGL subsystem error: GLEW initialization failed");
        return false;
    }
    LOG_INFO("OpenGL subsystem: GLEW library initialized (version: %s)",
             glewGetString(GLEW_VERSION));

    glGenVertexArrays(1, &state->vao);
    glBindVertexArray(state->vao);

    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    state->ubo_state = 0;
    glGenBuffers(1, &state->ubo_state);
    glBindBuffer(GL_UNIFORM_BUFFER, state->ubo_state);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, state->ubo_state);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    state->pass_ubo = 0;
    glGenBuffers(1, &state->pass_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, state->pass_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 16, NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, state->pass_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    if (state->shader_path && is_preset_path(state->shader_path)) {
        if (state->allow_vertex_shaders || state->vertex_shader_path) {
            LOG_WARN("Vertex shader overrides are ignored for presets (%s)", state->shader_path);
        }

        if (state->image_path) {

            struct glwall_image img;
            if (load_png_rgba8(state->image_path, &img)) {
                glGenTextures(1, &state->source_image_texture);
                glBindTexture(GL_TEXTURE_2D, state->source_image_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width_px, img.height_px, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, img.rgba);
                glBindTexture(GL_TEXTURE_2D, 0);
                state->source_image_width_px = img.width_px;
                state->source_image_height_px = img.height_px;
                free_glwall_image(&img);
            } else {
                LOG_WARN("Failed to load --image '%s' (PNG only); continuing with dummy Source",
                         state->image_path);
            }
        }

        if (!pipeline_init_from_preset(state, state->shader_path)) {
            LOG_ERROR("Failed to initialize preset pipeline from '%s'", state->shader_path);
            return false;
        }

        if (!init_audio(state)) {
            LOG_WARN("%s", "Audio subsystem initialization failed, audio disabled");
            state->audio_enabled = false;
        }

        LOG_DEBUG(state, "%s", "OpenGL subsystem initialization completed successfully (preset)");
        return true;
    }

    char *frag_src = NULL;
    if (state->shader_path) {
        char *raw_frag_src = read_file(state->shader_path);
        if (!raw_frag_src)
            return false;

        char *stripped_frag_src = strip_version_directive(raw_frag_src);
        free(raw_frag_src);
        if (!stripped_frag_src)
            return false;

        frag_src = concat_preamble(fragment_preamble, stripped_frag_src);
        free(stripped_frag_src);
    } else {

        const char *passthrough_src = "void main() {\n"
                                      "    fragColor = v_color;\n"
                                      "}\n";
        frag_src = concat_preamble(fragment_preamble, passthrough_src);
    }

    if (!frag_src)
        return false;

    char *vert_src = NULL;
    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        char *raw_vert_src = read_file(state->vertex_shader_path);
        if (!raw_vert_src) {
            LOG_ERROR("File operation failed: unable to read vertex shader '%s'",
                      state->vertex_shader_path);
            free(frag_src);
            return false;
        }

        char *stripped_vert_src = strip_version_directive(raw_vert_src);
        free(raw_vert_src);
        if (!stripped_vert_src) {
            free(frag_src);
            return false;
        }

        vert_src = concat_preamble(vertex_preamble, stripped_vert_src);
        free(stripped_vert_src);
        if (!vert_src) {
            free(frag_src);
            return false;
        }
    }

    const char *vs = vert_src ? vert_src : vertex_shader_src;
    state->shader_program = create_shader_program(state, vs, frag_src);
    free(frag_src);
    if (vert_src)
        free(vert_src);

    if (!state->shader_program)
        return false;

    state->current_program = 0;

    state->loc_resolution = glGetUniformLocation(state->shader_program, "iResolution");
    state->loc_resolution_vec2 = glGetUniformLocation(state->shader_program, "resolution");

    state->loc_time = glGetUniformLocation(state->shader_program, "iTime");
    if (state->loc_time == -1)
        state->loc_time = glGetUniformLocation(state->shader_program, "time");

    state->loc_time_delta = glGetUniformLocation(state->shader_program, "iTimeDelta");
    state->loc_frame = glGetUniformLocation(state->shader_program, "iFrame");

    state->loc_mouse = glGetUniformLocation(state->shader_program, "iMouse");
    state->loc_mouse_vec2 = glGetUniformLocation(state->shader_program, "mouse");

    state->loc_sound = glGetUniformLocation(state->shader_program, "sound");
    state->loc_sound_res = glGetUniformLocation(state->shader_program, "soundRes");
    state->loc_vertex_count = glGetUniformLocation(state->shader_program, "vertexCount");

    if (!init_audio(state)) {
        LOG_WARN("%s", "Audio subsystem initialization failed, audio disabled");
        state->audio_enabled = false;
    }

    state->profiling_enabled = getenv("GLWALL_PROFILE") != NULL;

    /* Install a simple signal handler to request a GPU timing dump. The actual
     * dump is performed on the main thread (in render_frame) to avoid doing
     * complex I/O inside an async signal handler. */
    signal(SIGUSR1, glwall_profile_signal_handler);

    if (state->loc_sound != -1) {
        if (state->current_program != state->shader_program) {
            glUseProgram(state->shader_program);
            state->current_program = state->shader_program;
        }
        glUniform1i(state->loc_sound, 0);
        glUseProgram(0);
        state->current_program = 0;
    }

    LOG_DEBUG(state, "%s", "OpenGL subsystem initialization completed successfully");
    return true;
}

void cleanup_opengl(struct glwall_state *state) {
    LOG_DEBUG(state, "%s", "OpenGL subsystem cleanup initiated");

    cleanup_audio(state);

    pipeline_cleanup(state);

    if (state->source_image_texture) {
        glDeleteTextures(1, &state->source_image_texture);
        state->source_image_texture = 0;
    }

    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
    }
    state->current_program = 0;
    if (state->vao) {
        glDeleteVertexArrays(1, &state->vao);
    }
    if (state->ubo_state) {
        glDeleteBuffers(1, &state->ubo_state);
        state->ubo_state = 0;
    }
    if (state->pass_ubo) {
        glDeleteBuffers(1, &state->pass_ubo);
        state->pass_ubo = 0;
    }
}

void render_frame(struct glwall_output *output) {
    assert(output != NULL);
    assert(output->state != NULL);

    struct glwall_state *state = output->state;
    if (!output->configured) {
        LOG_DEBUG(state, "Render cycle: skipping unconfigured output %u", output->output_name);
        return;
    }

    LOG_DEBUG(state, "Render cycle: rendering output %u (dimensions: %u x %u)", output->output_name,
              output->width_px, output->height_px);

    if (state->kernel_input_enabled) {
        poll_input_events(state);
    }

    eglMakeCurrent(state->egl_display, output->egl_surface, output->egl_surface,
                   state->egl_context);

    glBindVertexArray(state->vao);

    glViewport(0, 0, output->width_px, output->height_px);

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    float time_sec = (current_time.tv_sec - state->start_time.tv_sec) +
                     (current_time.tv_nsec - state->start_time.tv_nsec) / 1e9f;

    float dt_real;
    if (state->frame_index == 0) {
        dt_real = 0.0f;
        state->last_time_sec = time_sec;
    } else {
        dt_real = time_sec - state->last_time_sec;
    }

    float min_dt = 0.0f;
    switch (state->power_mode) {
    case GLWALL_POWER_MODE_FULL:
        min_dt = 0.0f;
        break;
    case GLWALL_POWER_MODE_THROTTLED:
        min_dt = 1.0f / 30.0f;
        break;
    case GLWALL_POWER_MODE_PAUSED:
        min_dt = 1.0f;
        break;
    default:
        min_dt = 0.0f;
        break;
    }

    bool do_update = (state->frame_index == 0) || (dt_real >= min_dt);
    float time_delta = 0.0f;
    if (do_update) {
        state->logical_time_sec += dt_real;
        time_delta = dt_real;
        state->last_time_sec = time_sec;
        state->frame_index++;
    }

    float shader_time = state->logical_time_sec;
    int current_frame = state->frame_index;

    if (do_update) {
        update_audio_texture(state);
    }

    if (pipeline_is_active(state)) {
        if (do_update) {
        }
        if (state->profiling_enabled) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            pipeline_render_frame(output, shader_time, time_delta, current_frame);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
            state->profiling_last_frame_ms = ms;
            LOG_INFO("Pipeline frame CPU time: %.3f ms", ms);
        } else {
            pipeline_render_frame(output, shader_time, time_delta, current_frame);
        }

        eglSwapBuffers(state->egl_display, output->egl_surface);
        LOG_DEBUG(state, "Render cycle: buffer swap completed for output %u", output->output_name);

        struct wl_callback *cb = wl_surface_frame(output->wl_surface);
        wl_callback_add_listener(cb, &frame_listener, output);
        wl_surface_commit(output->wl_surface);
        /* If an external signal requested a GPU timing dump, perform it now from the main
         * thread and write results to a file for later inspection. */
        if (glwall_dump_gpu_flag) {
            char path[PATH_MAX];
            const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
            pid_t pid = getpid();
            if (xdg_runtime && xdg_runtime[0] != '\0') {
                snprintf(path, sizeof(path), "%s/glwall_gpu_timing.%d.log", xdg_runtime, (int)pid);
            } else {
                snprintf(path, sizeof(path), "/tmp/glwall_gpu_timing.%d.log", (int)pid);
            }
            pipeline_dump_gpu_timing(state, path);
            LOG_INFO("GPU timing dump written to %s", path);
            glwall_dump_gpu_flag = 0;
        }
        return;
    }

    if (state->current_program != state->shader_program) {
        glUseProgram(state->shader_program);
        state->current_program = state->shader_program;
    }

    if (!state->ubo_state) {
        if (state->loc_time != -1) {
            glUniform1f(state->loc_time, shader_time);
        }
        if (state->loc_time_delta != -1) {
            glUniform1f(state->loc_time_delta, time_delta);
        }
        if (state->loc_frame != -1) {
            glUniform1i(state->loc_frame, current_frame);
        }
    }

    if (output->loc_resolution_last_updated == 0 || output->last_resolution_w != output->width_px ||
        output->last_resolution_h != output->height_px) {
        if (!state->ubo_state) {
            if (state->loc_resolution != -1) {
                glUniform3f(state->loc_resolution, (float)output->width_px,
                            (float)output->height_px, 1.0f);
            }
            if (state->loc_resolution_vec2 != -1) {
                glUniform2f(state->loc_resolution_vec2, (float)output->width_px,
                            (float)output->height_px);
            }
        }
        output->last_resolution_w = output->width_px;
        output->last_resolution_h = output->height_px;
        output->loc_resolution_last_updated = 1;
    }

    if (state->loc_mouse != -1 || state->loc_mouse_vec2 != -1) {
        float mx = 0.0f, my = 0.0f, mz = 0.0f, mw = 0.0f;

        if (state->kernel_input_enabled || state->pointer_output == output) {
            mx = (float)state->pointer_x;
            my = (float)(output->height_px - 1) - (float)state->pointer_y;
            if (state->pointer_down) {
                mz = (float)state->pointer_down_x;
                mw = (float)(output->height_px - 1) - (float)state->pointer_down_y;
            }
        }
        if (!state->ubo_state) {
            if (state->loc_mouse != -1) {
                glUniform4f(state->loc_mouse, mx, my, mz, mw);
            }
            if (state->loc_mouse_vec2 != -1) {
                glUniform2f(state->loc_mouse_vec2, mx, my);
            }
        }
        if (state->ubo_state) {
            float ubo_data[12];
            ubo_data[0] = (float)output->width_px;
            ubo_data[1] = (float)output->height_px;
            ubo_data[2] = 1.0f;
            ubo_data[3] = 0.0f;

            ubo_data[4] = shader_time;
            ubo_data[5] = time_delta;
            ubo_data[6] = (float)current_frame;
            ubo_data[7] = 0.0f;

            ubo_data[8] = mx;
            ubo_data[9] = my;
            ubo_data[10] = mz;
            ubo_data[11] = mw;

            glBindBuffer(GL_UNIFORM_BUFFER, state->ubo_state);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ubo_data), ubo_data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    }

    if (state->audio_enabled && state->audio.backend_ready) {
        if (state->audio.texture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state->audio.texture);
        }
    }

    if (state->loc_vertex_count != -1 && state->allow_vertex_shaders) {
        glUniform1f(state->loc_vertex_count, (float)state->vertex_count);
    }

    assert(output->width_px > 0 && output->height_px > 0);
    LOG_DEBUG(state,
              "Render cycle: shader uniforms set (iTime: %.2f, iTimeDelta: %.4f, iFrame: %d, "
              "iResolution: %d x %d x 1.0)",
              shader_time, time_delta, current_frame, output->width_px, output->height_px);

    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        glDrawArrays(state->vertex_draw_mode, 0, state->vertex_count);
    } else {

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    if (state->debug) {
        GLenum err;
        if ((err = glGetError()) != GL_NO_ERROR) {
            LOG_ERROR("OpenGL subsystem error: render error detected (error code: 0x%x)", err);
        }
    }

    eglSwapBuffers(state->egl_display, output->egl_surface);
    LOG_DEBUG(state, "Render cycle: buffer swap completed for output %u", output->output_name);

    struct wl_callback *cb = wl_surface_frame(output->wl_surface);
    wl_callback_add_listener(cb, &frame_listener, output);
    wl_surface_commit(output->wl_surface);
}