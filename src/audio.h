#pragma once

#include "state.h"

bool init_audio(struct glwall_state *state);

void update_audio_texture(struct glwall_state *state);

void cleanup_audio(struct glwall_state *state);
