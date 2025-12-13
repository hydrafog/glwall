#pragma once

#include "state.h"

#include <complex.h>

bool init_audio(struct glwall_state *state);

void update_audio_texture(struct glwall_state *state);

void cleanup_audio(struct glwall_state *state);

void audio_fft_process(float complex *data, int n);

int audio_read_recent_samples(struct glwall_state *state, int16_t *out, size_t count);
void audio_test_overwrite_ring(struct glwall_state *state, const int16_t *samples, size_t count);
