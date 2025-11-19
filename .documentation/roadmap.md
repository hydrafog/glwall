# Roadmap

<!-- Context Type: Planning & Status -->
<!-- Scope: Future plans, active tasks, and technical debt. -->

## Table of Contents
- [1. Completed Milestones](#1-completed-milestones)
- [2. Active Development (Short Term)](#2-active-development-short-term)
- [3. Planned Features (Medium Term)](#3-planned-features-medium-term)
- [4. Strategic Goals (Long Term)](#4-strategic-goals-long-term)

## 1. Completed Milestones

### 1.1. Documentation Overhaul
- [x] **Toolkit Alignment**: Re-architected documentation to strictly follow the project toolkit standards (HTML metadata, numbered sections).
- [x] **AI Context**: Created `.ai/` directory with `CODING_STYLE.md`, `CONTEXT.md`, and `GLOSSARY.md` to aid AI agents.
- [x] **License**: Transitioned project to **The Unlicense** (Public Domain).

### 1.2. Codebase Refactoring
- [x] **Standardization**: Enforced C99/C11 standards.
- [x] **Header Guards**: Migrated all headers to `#pragma once`.
- [x] **Type Safety**: Refactored state and utility functions to use fixed-width integers (`int32_t`) for critical dimensions.
- [x] **Core Rendering Pipeline**: Implemented Wayland + wlr-layer-shell integration, multi-output background layer surfaces, EGL context creation, and an OpenGL render loop driven by frame callbacks.
- [x] **Shader Uniforms & Inputs**: Added ShaderToy-like uniforms (`iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iMouse`) plus audio uniforms (`sound`, `soundRes`) and optional kernel input for richer interaction.
- [x] **Vertex Shader Support**: Added optional custom vertex shader pipeline with configurable vertex count and draw mode (points/lines).
- [x] **Power Modes**: Implemented `full`, `throttled`, and `paused` power modes that control logical shader time and update frequency.

### 1.3. Audio & Example Content
- [x] **Audio Backend**: Implemented PulseAudio-based capture, FFT, and an audio texture feeding shaders via `sound`/`soundRes`.
- [x] **Example Shaders**: Added an audio-reactive vertex/fragment pair (`audio-circles.vert` / `audio-circles.frag`) and a generic shader template (`shaders/template.glsl`) with documented uniforms for authoring new effects.
- [x] **Shader Error Logging**: Implemented detailed shader compilation and program-link error logging to stderr via `LOG_ERROR`, including driver info logs.

## 2. Active Development (Short Term)

### 2.1. Shadertoy Compatibility
- [ ] **Channel Inputs**: Support `iChannel0`..`iChannel3` uniforms for textures.
- [ ] **Texture Loading**: Implement `stb_image` integration to load PNG/JPG textures.

### 2.2. Robustness
- [ ] **Error Handling**: Graceful degradation when Wayland globals (like `zwlr_layer_shell_v1`) are missing.
- [x] **Validation**: Add shader compilation error logging to the overlay or stdout.

### 2.3. Content
- [ ] **Example Shaders**: Port 5-10 popular Shadertoy shaders to GLWall format.

## 3. Planned Features (Medium Term)

### 3.1. Configuration
- [ ] **Config File**: Implement a configuration file parser (TOML or JSON) to replace the unwieldy CLI arguments.
    *   *Goal*: `glwall -c ~/.config/glwall/config.toml`
- [ ] **Hot Reloading**: Auto-reload shader and config when files change.

### 3.2. User Experience
- [ ] **GUI Configurator**: A simple GTK/Qt app to select shaders and tweak uniforms.
- [ ] **Tray Icon**: Status icon to pause/resume rendering.

### 3.3. Advanced Rendering
- [ ] **Video Support**: Integrate `ffmpeg` to support video wallpapers (mp4/webm) instead of just GLSL.

## 4. Strategic Goals (Long Term)

### 4.1. Modernization
- [ ] **Rust Rewrite**: Port the core C codebase to Rust to ensure memory safety and concurrency.
    *   *Dependency*: `wayland-client-rs`, `glow` (OpenGL).

### 4.2. Platform Expansion
- [ ] **X11 Support**: Add an XCB/Xlib backend (Low Priority).
- [ ] **macOS Support**: Metal backend (Very Low Priority).
