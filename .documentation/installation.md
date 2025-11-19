# Installation & Setup Guide

<!-- Context Type: Onboarding & Deployment -->
<!-- Scope: Environment verification, dependency installation, and build steps. -->

## Table of Contents
- [1. System Prerequisites](#1-system-prerequisites)
- [2. Nix Installation (Recommended)](#2-nix-installation-recommended)
- [3. Manual Build](#3-manual-build)

## 1. System Prerequisites

The following tools must be installed and available in the system `PATH` before proceeding.

| Tool | Min Version | Check Command |
| :--- | :--- | :--- |
| **Wayland Compositor** | Any | `echo $XDG_SESSION_TYPE` |
| **OpenGL** | 3.3+ | `glxinfo \| grep "OpenGL version"` |
| **Nix** (Optional) | 2.18+ | `nix --version` |
| **GCC/Clang** (Manual) | Any | `gcc --version` |
| **Make** (Manual) | Any | `make --version` |

## 2. Nix Installation (Recommended)

GLWall is designed with Nix in mind for reproducible builds.

### 2.1. Run Directly
Run the application without installing it to your profile:
```bash
nix run github:yourusername/glwall
```

### 2.2. Install to Profile
Install the binary to your user profile:
```bash
nix profile install github:yourusername/glwall
```

### 2.3. NixOS Configuration
Add the flake to your system configuration and enable the module:

```nix
# flake.nix
{
  inputs.glwall.url = "github:yourusername/glwall";
  
  outputs = { self, nixpkgs, glwall, ... }: {
    nixosConfigurations.my-machine = nixpkgs.lib.nixosSystem {
      modules = [
        glwall.nixosModules.default
        {
          services.glwall.enable = true;
        }
      ];
    };
  };
}
```

## 3. Manual Build

If you are not using Nix, you can build from source using Make.

### 3.1. Dependencies

You will need a C compiler and the following development libraries:

*   `wayland` (client and protocols)
*   `wayland-protocols`
*   `wlr-protocols` (for layer-shell)
*   `glew`
*   `egl`
*   `libgl`
*   `pulseaudio` (libpulse)
*   `libevdev`

On Debian/Ubuntu:
```bash
sudo apt install build-essential libwayland-dev wayland-protocols \
    libglew-dev libegl1-mesa-dev libgl1-mesa-dev \
    libpulse-dev libevdev-dev
```

### 3.2. Build Steps

1.  Clone the repository:
    ```bash
    git clone https://github.com/yourusername/glwall.git
    cd glwall
    ```

2.  Build:
    ```bash
    cd src
    make
    ```

3.  Install (Optional):
    ```bash
    sudo cp glwall /usr/local/bin/
    ```
