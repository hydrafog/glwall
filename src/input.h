/**
 * @file input.h
 * @brief Kernel-level input device handling via libevdev
 *
 * This module reads mouse/pointer events directly from /dev/input/event* devices,
 * bypassing the Wayland compositor's input restrictions. This allows capturing
 * mouse input even when the wallpaper is behind other windows.
 *
 * Requires:
 * - libevdev library
 * - Read access to /dev/input/event* (usually via 'input' group membership)
 */

#pragma once

#include "state.h"

/**
 * @brief Initializes kernel input device monitoring
 *
 * Scans /dev/input/event* devices to find pointer devices and sets up
 * libevdev monitoring. Updates state->pointer_x/y directly from kernel events.
 *
 * @param state Pointer to global application state
 * @return true on success, false on failure (will log warnings)
 */
bool init_input(struct glwall_state *state);

/**
 * @brief Polls for kernel input events
 *
 * Non-blocking check for new input events from monitored devices.
 * Updates state->pointer_x/y and state->pointer_down when events occur.
 *
 * Should be called regularly (e.g., before each render frame).
 *
 * @param state Pointer to global application state
 */
void poll_input_events(struct glwall_state *state);

/**
 * @brief Cleans up input device resources
 *
 * Closes file descriptors and frees libevdev structures.
 *
 * @param state Pointer to global application state
 */
void cleanup_input(struct glwall_state *state);
