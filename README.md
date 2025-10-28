# GLWall

**GLSL shader wallpaper renderer for Wayland compositors**

GLWall renders GLSL fragment shaders as live wallpapers on Wayland desktops. Native Wayland client using `wlr-layer-shell` protocol for proper background layer integration.

## Features

- **Native Wayland**: Uses `wlr-layer-shell-unstable-v1` for background layer placement
- **Multi-Monitor**: Automatic detection and rendering to all displays
- **OpenGL 3.3**: Modern shader support with ShaderToy-compatible uniforms
- **Minimal**: Only essential system libraries required

## Quick Start

### With Nix Flakes

```bash
# Run directly
nix run github:hyperfog/glwall -- -s shaders/template.glsl

# Install
nix profile install github:hyperfog/glwall
```

### Build from Source

```bash
# Enter development shell
nix develop

# Build and run
cd src && make
./glwall -s ../shaders/template.glsl --debug
```

## Usage

```bash
glwall -s <shader-path> [--debug]
```

**Options:**
- `-s, --shader`: Path to GLSL fragment shader (required)
- `-d, --debug`: Enable debug logging

## Shader Format

GLSL 330 fragment shaders with ShaderToy-compatible uniforms:

```glsl
#version 330 core
out vec4 FragColor;
uniform vec3 iResolution;  // Viewport resolution
uniform float iTime;       // Time in seconds

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
}

void main() {
    mainImage(FragColor, gl_FragCoord.xy);
}
```

See `shaders/template.glsl` for a complete example.

## NixOS Module

```nix
{
  inputs.glwall.url = "github:yourusername/glwall";
  
  outputs = { glwall, ... }: {
    nixosConfigurations.hostname = {
      modules = [
        glwall.nixosModules.default
        {
          glwall.enable = true;
          glwall.shaderPath = "${glwall}/shaders/template.glsl";
        }
      ];
    };
  };
}
```

## Requirements

- Wayland compositor with `wlr-layer-shell-unstable-v1` support (Hyprland, Sway, etc.)
- OpenGL 3.3+ capable GPU
- For manual builds: `gcc`, `make`, `wayland`, `mesa`, `glew`, `wlr-protocols`

## Troubleshooting

**No wallpaper visible:**
- Verify compositor supports `wlr-layer-shell-unstable-v1`
- Check shader path is correct
- Run with `--debug` for detailed logs

**Build errors:**
- Use `nix develop` to enter development shell
- Ensure `WAYLAND_PROTOCOLS_DIR` and `WLR_PROTOCOLS_DIR` are set

## Architecture

- **Wayland-Native**: Direct `wayland-client.h` usage, no compatibility layers
- **Layer Shell**: Background layer placement via `wlr-layer-shell-unstable-v1`
- **EGL Context**: OpenGL context creation via `egl-wayland`
- **Frame Callbacks**: Compositor-driven rendering for optimal performance

## License

MIT License - See LICENSE file for details
