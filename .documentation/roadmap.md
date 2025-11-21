# Roadmap

<!-- Context Type: Planning & Status -->
<!-- Scope: Future plans, active tasks, and technical debt. -->

## Table of Contents
- [1. Completed Milestones](#1-completed-milestones)
- [2. Active Development (Short Term)](#2-active-development-short-term)
- [3. Planned Features (Medium Term)](#3-planned-features-medium-term)
- [4. Strategic Goals (Long Term)](#4-strategic-goals-long-term)

## 1. Completed Milestones

### 1.1. Enterprise Grade Quality
- [x] **Code Style Enforcement**: Standardized indentation, brace style, and naming conventions across the entire codebase.
- [x] **Robustness & Safety**: Audited and fixed memory allocation checks, system call error handling, and resource cleanup.
- [x] **Build System**: Integrated `pkg-config` for robust dependency management and reproducible builds.
- [x] **Documentation**: Comprehensive Doxygen comments for all public APIs and file-level documentation.

### 1.2. Core Architecture
- [x] **Wayland Integration**: Native wlr-layer-shell support for background and overlay layers.
- [x] **Rendering Pipeline**: High-performance OpenGL 3.3 Core Profile rendering loop driven by frame callbacks.
- [x] **Audio Reactivity**: PulseAudio backend with FFT analysis and texture generation.
- [x] **Input Handling**: Kernel-level input monitoring for mouse interaction behind windows.

### 1.3. Project Standards
- [x] **License**: The Unlicense (Public Domain).
- [x] **Structure**: Organized `src/`, `shaders/`, and `.documentation/` structure.

## 2. Active Development (Short Term)

### 2.1. Shadertoy Compatibility
- [ ] **Channel Inputs**: Support `iChannel0`..`iChannel3` uniforms for textures.
- [ ] **Texture Loading**: Implement `stb_image` integration to load PNG/JPG textures.

### 2.2. Advanced Configuration
- [ ] **Config File**: Implement TOML/JSON config parsing to replace CLI arguments.
- [ ] **Hot Reloading**: Auto-reload shader and config when files change.

## 3. Planned Features (Medium Term)

### 3.1. User Experience
- [ ] **GUI Configurator**: A simple GTK/Qt app to select shaders and tweak uniforms.
- [ ] **Tray Icon**: Status icon to pause/resume rendering.

### 3.2. Advanced Rendering
- [ ] **Video Support**: Integrate `ffmpeg` to support video wallpapers (mp4/webm).

## 4. Strategic Goals (Long Term)

### 4.1. Platform Expansion
- [ ] **X11 Support**: Add an XCB/Xlib backend.
- [ ] **Rust Rewrite**: Evaluate Rust for future core rewrite to ensure memory safety.
