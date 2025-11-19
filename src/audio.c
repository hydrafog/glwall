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

#include <pulse/simple.h>
#include <pulse/error.h>

#include <stdlib.h>
#include <string.h>

// Audio texture dimensions. We use a 1D "strip" texture of mono samples.
#define GLWALL_AUDIO_TEX_WIDTH 512
#define GLWALL_AUDIO_TEX_HEIGHT 1

/**
 * @brief Backend-specific audio implementation state
 *
 * Opaque structure storing PulseAudio connection and stream details.
 */
struct glwall_audio_impl {
    pa_simple *pa; /**< PulseAudio simple recording stream */
};

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
    bufattr.maxlength = (uint32_t) -1;
    bufattr.tlength = (uint32_t) -1;
    bufattr.prebuf = (uint32_t) -1;
    bufattr.minreq = (uint32_t) -1;
    // Fragment size: 512 samples = ~11ms at 44100 Hz (minimize latency)
    bufattr.fragsize = GLWALL_AUDIO_TEX_WIDTH * sizeof(int16_t);
    LOG_DEBUG(state, "Audio buffer fragsize: %u bytes", bufattr.fragsize);

    int error = 0;
    const char *device = state->audio_device_name;
    if (device) {
        LOG_INFO("Using audio device: %s", device);
    } else {
        LOG_INFO("Using default audio source");
    }
    pa_simple *pa = pa_simple_new(
        NULL,               // Use default server
        "glwall",          // Application name
        PA_STREAM_RECORD,   // Record stream
        device,             // Audio source device (NULL = default)
        "glwall-audio",    // Stream description
        &ss,
        NULL,               // Default channel map
        &bufattr,           // Low-latency buffering
        &error
    );

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

    state->audio.tex_width = GLWALL_AUDIO_TEX_WIDTH;
    state->audio.tex_height = GLWALL_AUDIO_TEX_HEIGHT;

    // Allocate texture storage with no initial data.
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_R32F,                       // Single float channel
        state->audio.tex_width,
        state->audio.tex_height,
        0,
        GL_RED,
        GL_FLOAT,
        NULL
    );

    state->audio.texture = tex;
    state->audio.enabled = true;
    state->audio.backend_ready = true;

    LOG_INFO("Audio texture created: %dx%d", state->audio.tex_width, state->audio.tex_height);
    LOG_DEBUG(state, "Audio backend initialized successfully");
    return true;
#endif
}

/**
 * @brief Update audio texture with latest samples
 *
 * Reads the latest audio samples from PulseAudio, normalizes them to
 * [-1, 1] range, and uploads them to the audio texture. Called once per
 * frame from the render loop. This function is a no-op if audio is
 * disabled or the backend is not ready.
 *
 * @param state Pointer to global application state
 *
 * @note The EGL context must be current before calling this function.
 * @note On read error from PulseAudio, the backend_ready flag is set
 *       to false to prevent repeated errors.
 */
void update_audio_texture(struct glwall_state *state) {
    if (!state->audio.enabled || !state->audio.backend_ready) return;
    if (!state->audio.impl) return;

    struct glwall_audio_impl *impl = state->audio.impl;
    if (!impl->pa) return;

    const int width = state->audio.tex_width;
    if (width <= 0 || state->audio.texture == 0) return;

    int error = 0;

    // Read raw samples from PulseAudio.
    int16_t samples[GLWALL_AUDIO_TEX_WIDTH];
    size_t bytes = sizeof(samples);

    if (pa_simple_read(impl->pa, samples, bytes, &error) < 0) {
        LOG_ERROR("PulseAudio read failed: %s", pa_strerror(error));
        state->audio.backend_ready = false;
        return;
    }

    // Convert samples to normalized floats in [-1, 1].
    float data[GLWALL_AUDIO_TEX_WIDTH];
    float max_sample = 0.0f;
    for (int i = 0; i < width; ++i) {
        data[i] = samples[i] / 32768.0f;
        float abs_val = data[i] < 0 ? -data[i] : data[i];
        if (abs_val > max_sample) max_sample = abs_val;
    }

    // Optionally log audio levels for debugging
    // static int frame_count = 0;
    // if (++frame_count % 120 == 0) {
    //     LOG_DEBUG(state, "Audio: max=%.4f", max_sample);
    // }

    glBindTexture(GL_TEXTURE_2D, state->audio.texture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        width,
        GLWALL_AUDIO_TEX_HEIGHT,
        GL_RED,
        GL_FLOAT,
        data
    );
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
void cleanup_audio(struct glwall_state *state) {
    glwall_audio_reset(state);
}
