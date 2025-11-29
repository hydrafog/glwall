#pragma once

#include "state.h"

bool init_opengl(struct glwall_state *state);

void cleanup_opengl(struct glwall_state *state);

void render_frame(struct glwall_output *output);
