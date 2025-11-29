#pragma once

#include "state.h"

bool init_wayland(struct glwall_state *state);

void cleanup_wayland(struct glwall_state *state);

void create_layer_surfaces(struct glwall_state *state);

void start_rendering(struct glwall_state *state);
