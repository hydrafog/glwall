# Project Context

<!-- Context Type: Knowledge Graph -->
<!-- Scope: High-level domain understanding and architectural philosophy. -->

## Table of Contents
- [1. Project Goal](#1-project-goal)
- [2. Core Philosophy](#2-core-philosophy)
- [3. Technical Constraints](#3-technical-constraints)

## 1. Project Goal

GLWall aims to provide a high-performance, resource-efficient wallpaper renderer for Wayland compositors. It bridges the gap between static wallpapers and heavy "live wallpaper" engines by using lightweight GLSL shaders.

## 2. Core Philosophy

*   **Unix Philosophy:** Do one thing well (render shaders on the background).
*   **Minimalism:** No GUI, no bloat. Configuration via CLI or Nix.
*   **Efficiency:** Don't burn CPU/GPU when not needed. Support pausing and throttling.

## 3. Technical Constraints

*   **Wayland Only:** No X11 support planned.
*   **Layer Shell:** Must strictly adhere to the `wlr-layer-shell` protocol.
*   **C Language:** Core logic must remain in C for performance and portability.
