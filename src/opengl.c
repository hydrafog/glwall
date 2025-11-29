#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <time.h>

#include "audio.h"
#include "input.h"
#include "opengl.h"
#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
                                       "#define gl_FragColor fragColor\n"
                                       "out vec4 fragColor;\n"
                                       "in vec4 v_color;\n";

static GLuint compile_shader(struct glwall_state *state, GLenum type, const char *source);
static GLuint create_shader_program(struct glwall_state *state, const char *vert_src,
                                    const char *frag_src);
static char *concat_preamble(const char *preamble, const char *source);

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

    LOG_DEBUG(state, "%s", "OpenGL subsystem initialization completed successfully");
    return true;
}

void cleanup_opengl(struct glwall_state *state) {
    LOG_DEBUG(state, "%s", "OpenGL subsystem cleanup initiated");

    cleanup_audio(state);

    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
    }
    if (state->vao) {
        glDeleteVertexArrays(1, &state->vao);
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

    glUseProgram(state->shader_program);

    if (state->loc_time != -1) {
        glUniform1f(state->loc_time, shader_time);
    }
    if (state->loc_time_delta != -1) {
        glUniform1f(state->loc_time_delta, time_delta);
    }
    if (state->loc_frame != -1) {
        glUniform1i(state->loc_frame, current_frame);
    }

    if (state->loc_resolution != -1) {
        glUniform3f(state->loc_resolution, (float)output->width_px, (float)output->height_px, 1.0f);
    }

    if (state->loc_resolution_vec2 != -1) {
        glUniform2f(state->loc_resolution_vec2, (float)output->width_px, (float)output->height_px);
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
        if (state->loc_mouse != -1) {
            glUniform4f(state->loc_mouse, mx, my, mz, mw);
        }
        if (state->loc_mouse_vec2 != -1) {
            glUniform2f(state->loc_mouse_vec2, mx, my);
        }
    }

    if (state->audio_enabled && state->audio.backend_ready) {
        if (state->loc_sound != -1 && state->audio.texture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, state->audio.texture);
            glUniform1i(state->loc_sound, 0);
        }
        if (state->loc_sound_res != -1) {
            glUniform2f(state->loc_sound_res, (float)state->audio.tex_width_px,
                        (float)state->audio.tex_height_px);
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

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (state->allow_vertex_shaders && state->vertex_shader_path) {
        glDrawArrays(state->vertex_draw_mode, 0, state->vertex_count);
    } else {

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    GLenum err;
    if ((err = glGetError()) != GL_NO_ERROR) {
        LOG_ERROR("OpenGL subsystem error: render error detected (error code: 0x%x)", err);
    }

    eglSwapBuffers(state->egl_display, output->egl_surface);
    LOG_DEBUG(state, "Render cycle: buffer swap completed for output %u", output->output_name);

    struct wl_callback *cb = wl_surface_frame(output->wl_surface);
    wl_callback_add_listener(cb, &frame_listener, output);
    wl_surface_commit(output->wl_surface);
}