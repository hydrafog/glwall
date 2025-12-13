#pragma once

#include "state.h"

#include <stdbool.h>
#include <stdint.h>

struct glwall_pipeline;

bool pipeline_init_from_preset(struct glwall_state *state, const char *preset_path);

void pipeline_cleanup(struct glwall_state *state);

bool pipeline_is_active(const struct glwall_state *state);

void pipeline_render_frame(struct glwall_output *output, float time_sec, float dt_sec,
                           int frame_index);

/* Dump aggregated GPU timings for all pipeline passes to `path`. Safe to call from
 * the main thread; does nothing if no pipeline is active. */
void pipeline_dump_gpu_timing(struct glwall_state *state, const char *path);
