# Configuration Reference

<!-- Context Type: Configuration & Env -->
<!-- Scope: CLI arguments, NixOS options, and shader uniforms. -->

## Table of Contents
- [1. Command Line Arguments](#1-command-line-arguments)
- [2. NixOS Module Options](#2-nixos-module-options)
- [3. Shader Uniforms](#3-shader-uniforms)

## 1. Command Line Arguments

Usage: `glwall -s <shader.frag> [options]`

| Argument | Type | Required | Default | Description |
| :--- | :--- | :--- | :--- | :--- |
| `-s, --shader` | Path | **Yes** | - | Path to the fragment shader file. |
| `-d, --debug` | Flag | No | `false` | Enable debug logging to stdout. |
| `-p, --power-mode` | Enum | No | `full` | `full`, `throttled`, or `paused`. |
| `-m, --mouse-overlay` | Enum | No | `none` | `none`, `edge`, or `full`. |
| `--audio` | Flag | No | `false` | Enable audio reactivity. |
| `--audio-source` | Enum | No | `pulse` | `pulse`, `pulseaudio`, or `none`. |
| `--vertex-count` | Int | No | `262144` | Number of vertices to draw. |

## 2. NixOS Module Options

When using the NixOS module, these options map to the CLI arguments.

| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `services.glwall.enable` | bool | `false` | Enable the service. |
| `services.glwall.shaderPath` | path | `...` | Path to fragment shader. |
| `services.glwall.powerMode` | enum | `"full"` | `full`, `throttled`, or `paused`. |
| `services.glwall.mouseOverlay` | enum | `"none"` | `none`, `edge`, or `full`. |
| `services.glwall.audio.enable` | bool | `true` | Enable audio reactivity. |

## 3. Shader Uniforms

GLWall provides the following uniforms to your GLSL shaders:

| Uniform | Type | Description |
| :--- | :--- | :--- |
| `u_time` | `float` | Time in seconds since start. |
| `u_resolution` | `vec2` | Screen resolution in pixels (width, height). |
| `u_mouse` | `vec2` | Mouse coordinates (normalized 0.0-1.0). |
| `u_audio_spectrum` | `sampler2D` | FFT audio data texture (if audio enabled). |
