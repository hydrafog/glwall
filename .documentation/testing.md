# Testing Strategy

<!-- Context Type: Quality Assurance -->
<!-- Scope: Testing protocols, manual verification, and automation plans. -->

## Table of Contents
- [1. Manual Verification](#1-manual-verification)
- [2. Future Automated Tests](#2-future-automated-tests)

## 1. Manual Verification

Currently, GLWall relies primarily on manual verification due to the visual nature of the output and the complexity of mocking Wayland/OpenGL environments.

To verify changes, run the application in debug mode with a known shader:

```bash
./glwall -s ../shaders/template.glsl --debug
```

### 1.1. Verification Checklist
1.  **Startup**: Does it launch without errors?
2.  **Display**: Is the wallpaper visible on all monitors?
3.  **Resize**: Does it handle monitor resolution changes?
4.  **Audio**: Does the shader react to system audio (if enabled)?
5.  **Input**: Does mouse interaction work (if enabled)?
6.  **Kernel Input**: Does mouse interaction work even when windows cover the wallpaper (with `--kernel-input`)?
7.  **Exit**: Does it shut down cleanly on Ctrl+C?

### 1.2. Testing Audio-Reactive Shaders

For testing audio-reactive shaders without real audio hardware or when you want predictable, reproducible audio data, use the fake audio source:

```bash
./glwall -s ../shaders/audio-circles.glsl --audio --audio-source fake --debug
```

The fake audio source generates synthetic audio with:
- Multiple frequency components (bass, mid, high)
- Amplitude modulation for dynamic spectrum
- Predictable, repeating patterns

This is useful for:
- Verifying audio texture uploads work correctly
- Testing shader audio reactivity without microphone/playback
- Creating reproducible test scenarios
- Debugging audio visualization issues

## 2. Future Automated Tests

We plan to implement:
*   **Headless EGL tests**: Verify shader compilation and uniform updates without a display.
*   **Wayland Mocking**: Use a library like `wlcs` to mock the compositor for protocol testing.
