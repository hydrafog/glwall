#pragma once

#include "state.h"

bool init_input(struct glwall_state *state);

void poll_input_events(struct glwall_state *state);

void cleanup_input(struct glwall_state *state);
