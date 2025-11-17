/**
 * @file audio.h
 * @brief Audio capture and OpenGL texture upload for GLWall
 *
 * This module provides a minimal audio pipeline for sound-reactive shaders.
 * It currently supports a PulseAudio backend and exposes audio data as a
 * 2D texture bound to the "sound" sampler2D uniform.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "state.h"

/**
 * @brief Initialize audio backend and create audio texture
 *
 * If audio is disabled or no supported backend is selected, this function
 * returns true without initializing anything so the caller can continue
 * gracefully.
 *
 * @param state Global application state
 * @return true on success, false on fatal error
 */
bool init_audio(struct glwall_state *state);

/**
 * @brief Update audio texture with latest samples
 *
 * Called once per frame from the render loop. If the backend is not ready
 * or audio is disabled, this function is a no-op.
 *
 * @param state Global application state
 */
void update_audio_texture(struct glwall_state *state);

/**
 * @brief Clean up audio backend and texture
 *
 * Frees backend resources and deletes the audio texture if it exists.
 *
 * @param state Global application state
 */
void cleanup_audio(struct glwall_state *state);

#endif // AUDIO_H
