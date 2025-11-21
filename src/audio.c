#define _POSIX_C_SOURCE 200809L
/**
 * @file audio.c
 * @brief PulseAudio-backed audio capture and OpenGL texture upload
 *
 * This module implements a minimal audio pipeline for GLWall. It captures
 * audio samples from the default PulseAudio source using the simple API
 * and uploads them into a 2D texture that shaders can sample via the
 * `sampler2D sound` uniform.
 */

// state.h must be included first to satisfy GL/EGL/Wayland include ordering.
#include "state.h"

#include "audio.h"
#include "utils.h"

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>
#include <pulse/simple.h>

#include <stdlib.h>
#include <string.h>

// Audio texture dimensions. We use a 1D "strip" texture of mono samples.
#define GLWALL_AUDIO_TEX_WIDTH 512
#define GLWALL_AUDIO_TEX_HEIGHT 240
#define GLWALL_AUDIO_NORMALIZATION 32768.0f

/**
 * @brief Backend-specific audio implementation state
 *
 * Opaque structure storing PulseAudio connection and stream details.
 */
struct glwall_audio_impl {
    pa_simple *pa; /**< PulseAudio simple recording stream */
    bool is_fake;  /**< True if using fake audio generation */
    float phase;   /**< Phase accumulator for fake audio sine wave */
};

// Helper for PulseAudio introspection
struct pa_monitor_data {
    char *monitor_source;
    pa_mainloop *mainloop;
};

static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    struct pa_monitor_data *data = userdata;
    if (eol < 0) {
        LOG_ERROR("Failed to get sink info: %s", pa_strerror(pa_context_errno(c)));
        return;
    }
    if (eol > 0)
        return;

    if (i && i->monitor_source_name) {
        if (data->monitor_source)
            free(data->monitor_source);
        data->monitor_source = strdup(i->monitor_source_name);
        if (!data->monitor_source) {
            LOG_ERROR("Failed to allocate memory for monitor source name");
            pa_mainloop_quit(data->mainloop, -1);
            return;
        }
        pa_mainloop_quit(data->mainloop, 0);
    }
}

static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    struct pa_monitor_data *data = userdata;
    if (!i) {
        LOG_ERROR("Failed to get server info: %s", pa_strerror(pa_context_errno(c)));
        pa_mainloop_quit(data->mainloop, -1);
        return;
    }

    // Query the default sink to get its monitor source
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

/**
 * @brief Get the monitor source name of the default sink
 *
 * Connects to PulseAudio, queries the default sink, and returns its monitor source name.
 * The caller must free the returned string.
 */
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

/**
 * @brief Resets audio state and cleans up backend
 *
 * Disables audio, deletes the audio texture, and frees all backend
 * resources. Sets state->audio.impl to NULL. This is used both during
 * initialization (on error) and cleanup.
 *
 * @param state Pointer to global application state
 */
static void glwall_audio_reset(struct glwall_state *state) {
    state->audio.enabled = false;
    state->audio.backend_ready = false;
    if (state->audio.texture != 0) {
        glDeleteTextures(1, &state->audio.texture);
        state->audio.texture = 0;
    }
    state->audio.tex_width = 0;
    state->audio.tex_height = 0;

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

/**
 * @brief Initialize audio backend and create audio texture
 *
 * Sets up the PulseAudio recording stream and creates an OpenGL texture
 * to store audio sample data. If audio is disabled or no backend is
 * selected, this function succeeds silently so the application can
 * continue without audio support.
 *
 * @param state Pointer to global application state
 * @return true on success or silent success (audio disabled), false on error
 *
 * @note The EGL context must be current before calling this function.
 * @note Audio texture dimensions are defined by GLWALL_AUDIO_TEX_WIDTH
 *       and GLWALL_AUDIO_TEX_HEIGHT macros.
 */
bool init_audio(struct glwall_state *state) {
    // Respect configuration: if audio is disabled or no backend selected,
    // silently succeed so the rest of the app can run.
    if (!state->audio_enabled || state->audio_source == GLWALL_AUDIO_SOURCE_NONE) {
        glwall_audio_reset(state);
        return true;
    }

    // Handle fake audio source for debugging/testing
    if (state->audio_source == GLWALL_AUDIO_SOURCE_FAKE) {
        LOG_INFO("Initializing fake audio backend for debugging...");
        
        struct glwall_audio_impl *impl = calloc(1, sizeof(struct glwall_audio_impl));
        if (!impl) {
            LOG_ERROR("Failed to allocate audio backend state.");
            glwall_audio_reset(state);
            return false;
        }
        impl->is_fake = true;
        impl->phase = 0.0f;
        impl->pa = NULL;
        state->audio.impl = impl;
        
        // Create OpenGL texture for audio data.
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
        
        state->audio.tex_width = GLWALL_AUDIO_TEX_WIDTH;
        state->audio.tex_height = GLWALL_AUDIO_TEX_HEIGHT;
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, state->audio.tex_width, state->audio.tex_height, 0,
                     GL_RED, GL_FLOAT, NULL);
        
        state->audio.texture = tex;
        state->audio.enabled = true;
        state->audio.backend_ready = true;
        
        LOG_INFO("Fake audio texture created: %dx%d", state->audio.tex_width, state->audio.tex_height);
        return true;
    }

    if (state->audio_source != GLWALL_AUDIO_SOURCE_PULSEAUDIO) {
        LOG_ERROR("Unsupported audio source selected.");
        glwall_audio_reset(state);
        return false;
    }

#ifdef GLWALL_DISABLE_PULSEAUDIO
    LOG_ERROR("PulseAudio support was disabled at build time.");
    glwall_audio_reset(state);
    return false;
#else
    LOG_INFO("Initializing PulseAudio backend for GLWall audio...");

    // Create PulseAudio simple recording stream from the default source.
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = 44100;
    ss.channels = 1; // mono for visualization simplicity

    // Low-latency buffer configuration
    pa_buffer_attr bufattr;
    bufattr.maxlength = (uint32_t)-1;
    bufattr.tlength = (uint32_t)-1;
    bufattr.prebuf = (uint32_t)-1;
    bufattr.minreq = (uint32_t)-1;
    // Let the server decide the fragment size to support low-latency configs
    bufattr.fragsize = (uint32_t)-1;
    LOG_DEBUG(state, "Audio buffer fragsize: auto");

    int error = 0;
    char *device = (char *)state->audio_device_name;
    char *monitor_source = NULL;

    if (device) {
        LOG_INFO("Using configured audio device: %s", device);
    } else {
        // Auto-detect default monitor source
        monitor_source = get_default_monitor_source();
        if (monitor_source) {
            LOG_INFO("Auto-detected monitor source: %s", monitor_source);
            device = monitor_source;
        } else {
            LOG_WARN("Failed to detect monitor source, falling back to default (microphone?)");
        }
    }

    pa_simple *pa = pa_simple_new(NULL,             // Use default server
                                  "glwall",         // Application name
                                  PA_STREAM_RECORD, // Record stream
                                  device,           // Audio source device (NULL = default)
                                  "glwall-audio",   // Stream description
                                  &ss,
                                  NULL,     // Default channel map
                                  &bufattr, // Low-latency buffering
                                  &error);

    if (monitor_source)
        free(monitor_source);

    if (!pa) {
        LOG_ERROR("Failed to create PulseAudio recording stream: %s", pa_strerror(error));
        glwall_audio_reset(state);
        return false;
    }

    struct glwall_audio_impl *impl = calloc(1, sizeof(struct glwall_audio_impl));
    if (!impl) {
        LOG_ERROR("Failed to allocate audio backend state.");
        pa_simple_free(pa);
        glwall_audio_reset(state);
        return false;
    }
    impl->pa = pa;
    state->audio.impl = impl;

    // Create OpenGL texture for audio data.
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Swizzle R -> A to support shaders that sample .a (alpha) for audio intensity
    GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

    state->audio.tex_width = GLWALL_AUDIO_TEX_WIDTH;
    state->audio.tex_height = GLWALL_AUDIO_TEX_HEIGHT;

    // Allocate texture storage with no initial data.
    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_R32F, // Single float channel
                 state->audio.tex_width, state->audio.tex_height, 0, GL_RED, GL_FLOAT, NULL);

    state->audio.texture = tex;
    state->audio.enabled = true;
    state->audio.backend_ready = true;

    LOG_INFO("Audio texture created: %dx%d", state->audio.tex_width, state->audio.tex_height);
    LOG_DEBUG(state, "Audio backend initialized successfully");
    return true;
#endif
}

#include <complex.h>
#include <math.h>

// FFT Parameters
#define GLWALL_FFT_SIZE 512
#define PI 3.14159265358979323846

/**
 * @brief Simple Cooley-Tukey FFT implementation
 *
 * @param data Input/Output complex array. Must be power of 2 size.
 * @param n Size of the array
 */
static void fft(float complex *data, int n) {
    if (n <= 1)
        return;

    // Split into even and odd
    float complex odd[n / 2];
    float complex even[n / 2];
    for (int i = 0; i < n / 2; i++) {
        even[i] = data[2 * i];
        odd[i] = data[2 * i + 1];
    }

    // Recursion
    fft(even, n / 2);
    fft(odd, n / 2);

    // Recombine
    for (int k = 0; k < n / 2; k++) {
        float complex t = cexp(-2.0 * I * PI * k / n) * odd[k];
        data[k] = even[k] + t;
        data[k + n / 2] = even[k] - t;
    }
}

/**
 * @brief Generate fake audio samples for debugging
 *
 * Creates synthetic audio with smooth, natural-sounding frequency content.
 * Generates a rich spectrum with multiple frequency bands and smooth modulation.
 *
 * @param impl Audio implementation state containing phase accumulator
 * @param samples Output buffer for generated samples
 * @param count Number of samples to generate
 */
static void generate_fake_audio(struct glwall_audio_impl *impl, int16_t *samples, int count) {
    const float sample_rate = 44100.0f;
    const float time_step = 1.0f / sample_rate;
    
    for (int i = 0; i < count; ++i) {
        float t = impl->phase;
        
        // Create smooth, natural-looking frequency content across the spectrum
        // Use multiple octaves with decreasing amplitude
        
        // Sub-bass (40-80 Hz) - slow, deep movement
        float sub = 0.15f * sinf(2.0f * PI * 50.0f * t) * 
                    (0.7f + 0.3f * sinf(2.0f * PI * 0.3f * t));
        
        // Bass (80-200 Hz) - main low-end energy
        float bass = 0.25f * sinf(2.0f * PI * 120.0f * t) * 
                     (0.6f + 0.4f * sinf(2.0f * PI * 0.7f * t));
        
        // Low-mid (200-500 Hz) - warmth
        float low_mid = 0.2f * sinf(2.0f * PI * 300.0f * t) * 
                        (0.5f + 0.5f * sinf(2.0f * PI * 1.1f * t));
        
        // Mid (500-2000 Hz) - presence
        float mid = 0.15f * sinf(2.0f * PI * 800.0f * t) * 
                    (0.4f + 0.6f * sinf(2.0f * PI * 1.7f * t));
        
        // High-mid (2-5 kHz) - clarity
        float high_mid = 0.12f * sinf(2.0f * PI * 3000.0f * t) * 
                         (0.3f + 0.7f * sinf(2.0f * PI * 2.3f * t));
        
        // High (5-10 kHz) - air/sparkle
        float high = 0.08f * sinf(2.0f * PI * 7000.0f * t) * 
                     (0.2f + 0.8f * sinf(2.0f * PI * 3.1f * t));
        
        // Add some harmonics for richness
        float harmonics = 0.05f * sinf(2.0f * PI * 150.0f * t) +
                         0.04f * sinf(2.0f * PI * 250.0f * t) +
                         0.03f * sinf(2.0f * PI * 450.0f * t);
        
        // Mix all bands
        float sample = sub + bass + low_mid + mid + high_mid + high + harmonics;
        
        // Apply smooth, natural envelope with varying speeds
        float env1 = 0.3f + 0.7f * sinf(2.0f * PI * 0.4f * t);
        float env2 = 0.5f + 0.5f * sinf(2.0f * PI * 0.9f * t);
        float envelope = env1 * env2;
        
        sample *= envelope;
        
        // Soft clipping for natural saturation
        if (sample > 0.8f) sample = 0.8f + (sample - 0.8f) * 0.2f;
        if (sample < -0.8f) sample = -0.8f + (sample + 0.8f) * 0.2f;
        
        // Convert to int16 with smooth scaling
        samples[i] = (int16_t)(sample * GLWALL_AUDIO_NORMALIZATION * 0.75f);
        
        impl->phase += time_step;
        if (impl->phase > 1000.0f) // Prevent phase from growing too large
            impl->phase -= 1000.0f;
    }
}

/**
 * @brief Update audio texture with latest samples and history
 *
 * Reads the latest audio samples, computes FFT, and updates the texture.
 * Row 0 contains the latest frequency data (FFT magnitudes).
 * Rows 1..N contain history (scrolled down).
 */
void update_audio_texture(struct glwall_state *state) {
    if (!state->audio.enabled || !state->audio.backend_ready)
        return;
    if (!state->audio.impl)
        return;

    struct glwall_audio_impl *impl = state->audio.impl;
    
    // Handle fake audio source
    if (impl->is_fake) {
        // Generate fake audio samples instead of reading from PulseAudio
        // Continue to the common FFT processing below
    } else if (!impl->pa) {
        return;
    }

    const int width = state->audio.tex_width;
    const int height = state->audio.tex_height;
    if (width <= 0 || height <= 0 || state->audio.texture == 0)
        return;

    // Acquire audio samples (real or fake)
    int16_t samples[GLWALL_FFT_SIZE];
    
    if (impl->is_fake) {
        // Generate synthetic audio samples
        generate_fake_audio(impl, samples, GLWALL_FFT_SIZE);
    } else {
        // Read real samples from PulseAudio
        int error = 0;
        size_t bytes = sizeof(samples);
        if (pa_simple_read(impl->pa, samples, bytes, &error) < 0) {
            LOG_ERROR("PulseAudio read failed: %s", pa_strerror(error));
            state->audio.backend_ready = false;
            return;
        }
    }

    // Debug: write raw samples to file for diagnosis
    static FILE *debug_file = NULL;
    static int frame_count = 0;
    if (state->debug && !debug_file) {
        debug_file = fopen("/tmp/glwall_audio_debug.txt", "w");
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

    // Prepare data for FFT
    float complex fft_data[GLWALL_FFT_SIZE];
    float rms_accum = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < GLWALL_FFT_SIZE; ++i) {
        // Normalize to [-1, 1]
        float sample = samples[i] / GLWALL_AUDIO_NORMALIZATION;

        float abs_sample = fabsf(sample);
        if (abs_sample > peak) {
            peak = abs_sample;
        }
        rms_accum += sample * sample;

        // Apply Hanning window to reduce spectral leakage
        float window = 0.5f * (1.0f - cosf(2.0f * PI * i / (GLWALL_FFT_SIZE - 1)));

        fft_data[i] = sample * window;
    }

    float rms = sqrtf(rms_accum / (float)GLWALL_FFT_SIZE);
    LOG_DEBUG(state, "Audio frame: peak=%.6f rms=%.6f", peak, rms);

    // Compute FFT
    fft(fft_data, GLWALL_FFT_SIZE);

    // Compute magnitudes for the first half (frequencies 0 to Nyquist)
    // We map this to the texture width.
    // FFT size 512 -> 256 frequency bins.
    // Our texture width is 512. We can interpolate or just fill half.
    // Let's fill 0..255 into 0..511 by duplicating or interpolating?
    // Or just compute FFT of size 1024?
    // Let's stick to 512 size and map bins to pixels.
    // 256 bins -> 512 pixels = 2 pixels per bin.

    // Smoothing factor (0.8 is typical for Web Audio API)
    const float smoothingTimeConstant = 0.8f;

    // Temporary buffer for current frame's smoothed data
    // We need to store persistent smoothed data.
    // Since we don't have a persistent buffer in impl yet, let's add static for now
    // or better, rely on the texture's previous value? No, texture is float but we overwrite it.
    // Let's use a static buffer here for simplicity, or add to impl struct if we were strict.
    static float smoothed_data[GLWALL_AUDIO_TEX_WIDTH] = {0};

    float new_row[GLWALL_AUDIO_TEX_WIDTH];
    for (int i = 0; i < GLWALL_AUDIO_TEX_WIDTH; ++i) {
        int bin_idx = i / 2; // Simple expansion
        if (bin_idx >= GLWALL_FFT_SIZE / 2)
            bin_idx = GLWALL_FFT_SIZE / 2 - 1;

        float mag = cabsf(fft_data[bin_idx]);

        // Avoid log(0) by enforcing a small floor
        if (mag < 1e-6f) {
            mag = 1e-6f;
        }

        // Convert to decibels relative to full-scale (1.0)
        float db = 20.0f * log10f(mag);

        // Map a narrower, more useful range [-60, 0] dB to [0, 1]
        const float minDecibels = -60.0f;
        const float maxDecibels = 0.0f;

        if (db < minDecibels)
            db = minDecibels;
        if (db > maxDecibels)
            db = maxDecibels;

        float normalized = (db - minDecibels) / (maxDecibels - minDecibels);

        // Apply smoothing
        // value = smoothing * old + (1 - smoothing) * new
        smoothed_data[i] =
            smoothingTimeConstant * smoothed_data[i] + (1.0f - smoothingTimeConstant) * normalized;

        new_row[i] = smoothed_data[i];
    }

    glBindTexture(GL_TEXTURE_2D, state->audio.texture);

    // Scroll history: Copy rows 0..H-2 to 1..H-1
    // This is expensive on GPU if done via readback.
    // Better to use glCopyTexSubImage2D to copy within texture?
    // Or just keep a CPU buffer of the whole history?
    // For 512x240, CPU buffer is small (512*240*4 bytes ~ 500KB).
    // Let's use a static buffer for simplicity in this C file.

    static float *history_buffer = NULL;
    if (!history_buffer) {
        history_buffer = calloc(width * height, sizeof(float));
        if (!history_buffer) {
            LOG_ERROR("Failed to allocate audio history buffer");
            return;
        }
    }

    // Shift history in CPU buffer
    // Move row 0 to row 1, etc.
    // memmove destination: buffer + width
    // source: buffer
    // size: width * (height - 1)
    if (height > 1) {
        memmove(history_buffer + width, history_buffer, width * (height - 1) * sizeof(float));
    }

    // Copy new row to top
    memcpy(history_buffer, new_row, width * sizeof(float));

    // Upload entire texture
    // Optimization: Use a ring buffer offset uniform instead of moving memory?
    // But shader expects row 0 to be latest.
    // For now, full upload is fine for 512x240.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_FLOAT, history_buffer);
}

/**
 * @brief Clean up audio backend and texture
 *
 * Frees all backend resources (PulseAudio connections, etc.) and deletes
 * the audio texture. Sets the audio state to disabled. This is typically
 * called during application shutdown.
 *
 * @param state Pointer to global application state
 */
void cleanup_audio(struct glwall_state *state) { glwall_audio_reset(state); }
