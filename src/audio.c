#define _POSIX_C_SOURCE 200809L

#include "state.h"
#include <assert.h>

#include "audio.h"
#include "utils.h"

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/simple.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GLWALL_AUDIO_TEX_WIDTH 512
#define GLWALL_AUDIO_TEX_HEIGHT 2
#define GLWALL_AUDIO_TEX_ROW_WAVEFORM 0
#define GLWALL_AUDIO_TEX_ROW_SPECTRUM 1
#define GLWALL_AUDIO_NORMALIZATION 32768.0f

struct glwall_audio_impl {
    pa_simple *pa;
    bool is_fake;
    float phase;
};

struct pa_monitor_data {
    char *monitor_source;
    pa_mainloop *mainloop;
};

static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    struct pa_monitor_data *data = userdata;
    if (eol < 0) {
        LOG_ERROR("PulseAudio operation failed: unable to retrieve sink information (error: %s)",
                  pa_strerror(pa_context_errno(c)));
        return;
    }
    if (eol > 0)
        return;

    if (i && i->monitor_source_name) {
        if (data->monitor_source)
            free(data->monitor_source);
        data->monitor_source = strdup(i->monitor_source_name);
        if (!data->monitor_source) {
            LOG_ERROR(
                "%s",
                "Memory allocation failed: insufficient memory for audio monitor source name");
            pa_mainloop_quit(data->mainloop, -1);
            return;
        }
        pa_mainloop_quit(data->mainloop, 0);
    }
}

static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    struct pa_monitor_data *data = userdata;
    if (!i) {
        LOG_ERROR("PulseAudio operation failed: unable to retrieve server information (error: %s)",
                  pa_strerror(pa_context_errno(c)));
        pa_mainloop_quit(data->mainloop, -1);
        return;
    }

    pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, data);
}

static void context_state_callback(pa_context *c, void *userdata) {
    struct pa_monitor_data *data = userdata;
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        pa_context_get_server_info(c, server_info_callback, data);
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        pa_mainloop_quit(data->mainloop, -1);
        break;
    default:
        break;
    }
}

static char *get_default_monitor_source(void) {
    pa_mainloop *m = pa_mainloop_new();
    pa_mainloop_api *api = pa_mainloop_get_api(m);
    pa_context *c = pa_context_new(api, "glwall-probe");

    struct pa_monitor_data data = {.monitor_source = NULL, .mainloop = m};

    pa_context_set_state_callback(c, context_state_callback, &data);
    pa_context_connect(c, NULL, 0, NULL);

    int ret;
    pa_mainloop_run(m, &ret);

    pa_context_disconnect(c);
    pa_context_unref(c);
    pa_mainloop_free(m);

    return data.monitor_source;
}

static void glwall_audio_reset(struct glwall_state *state) {
    state->audio.enabled = false;
    state->audio.backend_ready = false;
    if (state->audio.texture != 0) {
        glDeleteTextures(1, &state->audio.texture);
        state->audio.texture = 0;
    }
    state->audio.tex_width_px = 0;
    state->audio.tex_height_px = 0;

    if (state->audio.impl) {
        struct glwall_audio_impl *impl = state->audio.impl;
        if (impl->pa) {
            pa_simple_free(impl->pa);
            impl->pa = NULL;
        }
        free(impl);
        state->audio.impl = NULL;
    }
}

bool init_audio(struct glwall_state *state) {
    assert(state != NULL);

    if (!state->audio_enabled || state->audio_source == GLWALL_AUDIO_SOURCE_NONE) {
        glwall_audio_reset(state);
        return true;
    }

    if (state->audio_source == GLWALL_AUDIO_SOURCE_FAKE) {
        LOG_INFO("%s",
                 "Audio subsystem initialization: fake audio backend selected for diagnostics");

        struct glwall_audio_impl *impl = calloc(1, sizeof(struct glwall_audio_impl));
        if (!impl) {
            LOG_ERROR("%s",
                      "Memory allocation failed: insufficient memory for audio backend state");
            glwall_audio_reset(state);
            return false;
        }
        impl->is_fake = true;
        impl->phase = 0.0f;
        impl->pa = NULL;
        state->audio.impl = impl;

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

        state->audio.tex_width_px = GLWALL_AUDIO_TEX_WIDTH;
        state->audio.tex_height_px = GLWALL_AUDIO_TEX_HEIGHT;

        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, state->audio.tex_width_px,
                     state->audio.tex_height_px, 0, GL_RED, GL_FLOAT, NULL);

        state->audio.texture = tex;
        state->audio.enabled = true;
        state->audio.backend_ready = true;

        LOG_INFO("Audio resource created: texture (%dx%d) initialized for fake audio backend",
                 state->audio.tex_width_px, state->audio.tex_height_px);
        return true;
    }

    if (state->audio_source != GLWALL_AUDIO_SOURCE_PULSEAUDIO) {
        LOG_ERROR("%s", "Audio subsystem error: unsupported audio source selected");
        glwall_audio_reset(state);
        return false;
    }

#ifdef GLWALL_DISABLE_PULSEAUDIO
    LOG_ERROR("%s", "Audio subsystem error: PulseAudio support was disabled at build time");
    glwall_audio_reset(state);
    return false;
#else
    LOG_INFO("%s", "Audio subsystem initialization: PulseAudio backend initialization commenced");

    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100;
    ss.channels = 1;

    pa_buffer_attr bufattr;
    bufattr.maxlength = (uint32_t)-1;
    bufattr.tlength = (uint32_t)-1;
    bufattr.prebuf = (uint32_t)-1;
    bufattr.minreq = (uint32_t)-1;

    bufattr.fragsize = (uint32_t)-1;
    LOG_DEBUG(state, "%s", "Audio buffer fragsize: auto");

    int error = 0;
    char *device = (char *)state->audio_device_name;
    char *monitor_source = NULL;

    if (device) {
        LOG_INFO("Audio subsystem configuration: audio device '%s' specified", device);
    } else {

        monitor_source = get_default_monitor_source();
        if (monitor_source) {
            LOG_INFO("Audio subsystem detection: monitor source '%s' auto-detected",
                     monitor_source);
            device = monitor_source;
        } else {
            LOG_WARN(
                "%s",
                "Audio subsystem warning: unable to auto-detect monitor source, using default");
        }
    }

    pa_simple *pa = pa_simple_new(NULL, "glwall", PA_STREAM_RECORD, device, "glwall-audio", &ss,
                                  NULL, &bufattr, &error);

    if (monitor_source)
        free(monitor_source);

    if (!pa) {
        LOG_ERROR("PulseAudio operation failed: unable to create recording stream (error: %s)",
                  pa_strerror(error));
        glwall_audio_reset(state);
        return false;
    }

    struct glwall_audio_impl *impl = calloc(1, sizeof(struct glwall_audio_impl));
    if (!impl) {
        LOG_ERROR("%s", "Memory allocation failed: insufficient memory for audio backend state");
        pa_simple_free(pa);
        glwall_audio_reset(state);
        return false;
    }
    impl->pa = pa;
    state->audio.impl = impl;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

    state->audio.tex_width_px = GLWALL_AUDIO_TEX_WIDTH;
    state->audio.tex_height_px = GLWALL_AUDIO_TEX_HEIGHT;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, state->audio.tex_width_px, state->audio.tex_height_px,
                 0, GL_RED, GL_FLOAT, NULL);

    state->audio.texture = tex;
    state->audio.enabled = true;
    state->audio.backend_ready = true;

    LOG_INFO("Audio resource created: texture (%dx%d) for PulseAudio backend",
             state->audio.tex_width_px, state->audio.tex_height_px);
    LOG_DEBUG(state, "%s", "Audio subsystem initialization completed successfully");
    return true;
#endif
}

#include <complex.h>
#include <math.h>

#define GLWALL_FFT_SIZE 512
#define PI 3.14159265358979323846

static void fft(float complex *data, int n) {
    if (n <= 1)
        return;

    float complex odd[n / 2];
    float complex even[n / 2];
    for (int i = 0; i < n / 2; i++) {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }

    fft(even, n / 2);
    fft(odd, n / 2);

    for (int k = 0; k < n / 2; k++) {
        float complex t = cexp(-2.0 * I * PI * k / n) * odd[k];
        data[k] = even[k] + t;
        data[k + n / 2] = even[k] - t;
    }
}

static void generate_fake_audio(struct glwall_audio_impl *impl, int16_t *samples, int count) {
    const float sample_rate = 44100.0f;
    const float time_step = 1.0f / sample_rate;

    for (int i = 0; i < count; ++i) {
        float t = impl->phase;

        float sub =
            0.15f * sinf(2.0f * PI * 50.0f * t) * (0.7f + 0.3f * sinf(2.0f * PI * 0.3f * t));

        float bass =
            0.25f * sinf(2.0f * PI * 120.0f * t) * (0.6f + 0.4f * sinf(2.0f * PI * 0.7f * t));

        float low_mid =
            0.2f * sinf(2.0f * PI * 300.0f * t) * (0.5f + 0.5f * sinf(2.0f * PI * 1.1f * t));

        float mid =
            0.15f * sinf(2.0f * PI * 800.0f * t) * (0.4f + 0.6f * sinf(2.0f * PI * 1.7f * t));

        float high_mid =
            0.12f * sinf(2.0f * PI * 3000.0f * t) * (0.3f + 0.7f * sinf(2.0f * PI * 2.3f * t));

        float high =
            0.08f * sinf(2.0f * PI * 7000.0f * t) * (0.2f + 0.8f * sinf(2.0f * PI * 3.1f * t));

        float harmonics = 0.05f * sinf(2.0f * PI * 150.0f * t) +
                          0.04f * sinf(2.0f * PI * 250.0f * t) +
                          0.03f * sinf(2.0f * PI * 450.0f * t);

        float sample = sub + bass + low_mid + mid + high_mid + high + harmonics;

        float env1 = 0.3f + 0.7f * sinf(2.0f * PI * 0.4f * t);
        float env2 = 0.5f + 0.5f * sinf(2.0f * PI * 0.9f * t);
        float envelope = env1 * env2;

        sample *= envelope;

        if (sample > 0.8f)
            sample = 0.8f + (sample - 0.8f) * 0.2f;
        if (sample < -0.8f)
            sample = -0.8f + (sample + 0.8f) * 0.2f;

        samples[i] = (int16_t)(sample * GLWALL_AUDIO_NORMALIZATION * 0.75f);

        impl->phase += time_step;
        if (impl->phase > 1000.0f)
            impl->phase -= 1000.0f;
    }
}

void update_audio_texture(struct glwall_state *state) {
    assert(state != NULL);

    if (!state->audio.enabled || !state->audio.backend_ready)
        return;
    if (!state->audio.impl)
        return;

    struct glwall_audio_impl *impl = state->audio.impl;

    if (impl->is_fake) {

    } else if (!impl->pa) {
        return;
    }

    int width = state->audio.tex_width_px;
    int height = state->audio.tex_height_px;
    if (width <= 0 || height <= 0 || state->audio.texture == 0)
        return;

    int16_t samples[GLWALL_FFT_SIZE];

    if (impl->is_fake) {

        generate_fake_audio(impl, samples, GLWALL_FFT_SIZE);
    } else {

        int error = 0;
        size_t bytes = sizeof(samples);
        if (pa_simple_read(impl->pa, samples, bytes, &error) < 0) {
            LOG_ERROR("PulseAudio operation failed: read error (error: %s)", pa_strerror(error));
            state->audio.backend_ready = false;
            return;
        }
    }

    static FILE *debug_file = NULL;
    static int frame_count = 0;
    if (state->debug) {
        if (!debug_file) {

            const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
            char tmpl[PATH_MAX];
            if (xdg_runtime && xdg_runtime[0] != '\0') {
                snprintf(tmpl, sizeof(tmpl), "%s/glwall_audio_debug.XXXXXX", xdg_runtime);
            } else {
                snprintf(tmpl, sizeof(tmpl), "/tmp/glwall_audio_debug.XXXXXX");
            }
            int fd = mkstemp(tmpl);
            if (fd >= 0) {

                fchmod(fd, S_IRUSR | S_IWUSR);
                debug_file = fdopen(fd, "w");
                if (!debug_file) {
                    close(fd);
                }
            }
        }
        if (debug_file) {
            fprintf(debug_file, "Frame %d: [", frame_count);
            for (int i = 0; i < 16 && i < GLWALL_FFT_SIZE; i++) {
                fprintf(debug_file, "%d%s", samples[i], i < 15 ? ", " : "");
            }
            fprintf(debug_file, "]\n");
            fflush(debug_file);
            frame_count++;
        }
    }

    float complex fft_data[GLWALL_FFT_SIZE];
    float waveform_row[GLWALL_AUDIO_TEX_WIDTH] = {0};
    float rms_accum = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < GLWALL_FFT_SIZE; ++i) {

        float sample = samples[i] / GLWALL_AUDIO_NORMALIZATION;

        float abs_sample = fabsf(sample);
        if (abs_sample > peak) {
            peak = abs_sample;
        }
        rms_accum += sample * sample;

        if (i < GLWALL_AUDIO_TEX_WIDTH) {
            float normalized_wave = sample * 0.5f + 0.5f;
            if (normalized_wave < 0.0f)
                normalized_wave = 0.0f;
            if (normalized_wave > 1.0f)
                normalized_wave = 1.0f;
            waveform_row[i] = normalized_wave;
        }

        float window = 0.5f * (1.0f - cosf(2.0f * PI * i / (GLWALL_FFT_SIZE - 1)));

        fft_data[i] = sample * window;
    }

    float rms = sqrtf(rms_accum / (float)GLWALL_FFT_SIZE);
    LOG_DEBUG(state, "Audio frame: peak=%.6f rms=%.6f", peak, rms);

    fft(fft_data, GLWALL_FFT_SIZE);

    float spectrum_row[GLWALL_AUDIO_TEX_WIDTH];
    for (int i = 0; i < GLWALL_AUDIO_TEX_WIDTH; ++i) {
        int bin_idx = i / 2;
        if (bin_idx >= GLWALL_FFT_SIZE / 2)
            bin_idx = GLWALL_FFT_SIZE / 2 - 1;

        float mag = cabsf(fft_data[bin_idx]);

        float normalized = mag * 4.0f;
        if (normalized > 1.0f)
            normalized = 1.0f;

        spectrum_row[i] = normalized;
    }

    float audio_texture[GLWALL_AUDIO_TEX_WIDTH * GLWALL_AUDIO_TEX_HEIGHT];
    memcpy(audio_texture + (size_t)GLWALL_AUDIO_TEX_ROW_WAVEFORM * GLWALL_AUDIO_TEX_WIDTH,
           waveform_row, sizeof(waveform_row));
    memcpy(audio_texture + (size_t)GLWALL_AUDIO_TEX_ROW_SPECTRUM * GLWALL_AUDIO_TEX_WIDTH,
           spectrum_row, sizeof(spectrum_row));

    glBindTexture(GL_TEXTURE_2D, state->audio.texture);

    if (width != GLWALL_AUDIO_TEX_WIDTH || height != GLWALL_AUDIO_TEX_HEIGHT) {
        LOG_WARN("Audio subsystem: unexpected texture size (%dx%d), expected %dx%d", width, height,
                 GLWALL_AUDIO_TEX_WIDTH, GLWALL_AUDIO_TEX_HEIGHT);
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_FLOAT, audio_texture);
}

void cleanup_audio(struct glwall_state *state) { glwall_audio_reset(state); }
