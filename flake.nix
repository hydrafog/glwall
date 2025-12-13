{
  description = "GLWall - GLSL shader wallpaper renderer for Wayland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        glwallPackage = pkgs.stdenv.mkDerivation {
          pname = "glwall";
          version = "1.0.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            gcc
            gnumake
            glew
            libGL
            libpng
            wayland
            wayland-protocols
            wlr-protocols
            egl-wayland
            pulseaudio
            libevdev
          ];

          WAYLAND_PROTOCOLS_DIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
          WLR_PROTOCOLS_DIR = "${pkgs.wlr-protocols}/share/wlr-protocols";

          buildPhase = ''
            cd src
            make clean
            make \
              EXTRA_CFLAGS="-I${pkgs.libevdev}/include/libevdev-1.0" \
                LDFLAGS="-lGL -lGLEW -lEGL -lwayland-client -lwayland-egl -lm -lpulse-simple -lpulse -levdev -lpng"
          '';

          checkPhase = ''
            cd src
            test -x ./glwall
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp ./glwall $out/bin/
            mkdir -p $out/share/glwall/shaders
            cp -r ../shaders/* $out/share/glwall/shaders/
          '';

          meta = with pkgs.lib; {
            description = "A multi-monitor GLSL shader wallpaper renderer for Wayland";
            homepage = "https://github.com/hydrafog/glwall";
            license = licenses.unlicense;
            maintainers = [ ];
            platforms = platforms.linux;
          };
        };
      in
      {
        packages = {
          default = glwallPackage;
          glwall = glwallPackage;
        };

        apps.default = {
          type = "app";
          program = "${glwallPackage}/bin/glwall";
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ glwallPackage ];

          packages = with pkgs; [
            nodejs
            clang-tools
          ];

          shellHook = ''
            export WAYLAND_PROTOCOLS_DIR="${pkgs.wayland-protocols}/share/wayland-protocols"
            export WLR_PROTOCOLS_DIR="${pkgs.wlr-protocols}/share/wlr-protocols"
            export LD_LIBRARY_PATH="${pkgs.stdenv.cc.cc.lib}/lib:$LD_LIBRARY_PATH"

            if [ -z "$GLWALL_SHELL_INIT_DONE" ]; then
              export GLWALL_SHELL_INIT_DONE=1
              if [ -x ./scripts/start-service.sh ]; then
                ./scripts/start-service.sh >/dev/null 2>&1 || true
              fi
            fi
          '';
        };
      }
    ) // {
      nixosModules.default = { config, pkgs, lib, ... }:
        let
          cfg = config.glwall;
          glwallPkg = self.packages.${pkgs.system}.default;
        in
        {
          options.glwall = {
            enable = lib.mkEnableOption "GLWall shader wallpaper renderer";

            shaderPath = lib.mkOption {
              type = lib.types.str;
              description = "Path to the GLSL fragment shader file";
              default = "${glwallPkg}/share/glwall/shaders/retrowave.glsl";
            };

            package = lib.mkOption {
              type = lib.types.package;
              default = glwallPkg;
              description = "GLWall package to use";
            };

            powerMode = lib.mkOption {
              type = lib.types.enum [ "full" "throttled" "paused" ];
              default = "full";
              description = "Rendering power policy: full, throttled, or paused when occluded.";
            };

            mouseOverlay = lib.mkOption {
              type = lib.types.enum [ "none" "edge" "full" ];
              default = "none";
              description = "Experimental mouse overlay mode (none, edge, full).";
            };

            audio = {
              enable = lib.mkOption {
                type = lib.types.bool;
                default = true;
                description = "Enable audio-reactive shaders (sampler2D sound).";
              };

              source = lib.mkOption {
                type = lib.types.enum [ "pulseaudio" "fake" "none" ];
                default = "pulseaudio";
                description = "Audio backend to use for sound capture (pulseaudio for real audio, fake for synthetic debugging audio, none to disable).";
              };
            };

            shaders = {
              allowVertex = lib.mkOption {
                type = lib.types.bool;
                default = false;
                description = "Allow custom vertex shaders for advanced effects.";
              };
            };

            vertexCount = lib.mkOption {
              type = lib.types.int;
              default = 262144;
              description = "Number of vertices to draw when using a custom vertex shader.";
            };

            mouseOverlayHeight = lib.mkOption {
              type = lib.types.int;
              default = 32;
              description = "Height in pixels of the mouse overlay edge strip when mouseOverlay = 'edge'.";
            };
          };

          config = lib.mkIf cfg.enable {
            assertions = [
              {
                assertion = lib.hasPrefix "${glwallPkg}/share/glwall/shaders" cfg.shaderPath;
                message = "glwall.shaderPath must point inside ${glwallPkg}/share/glwall/shaders.";
              }
            ];

            environment.systemPackages = [ cfg.package ];

            systemd.user.services.glwall = {
              description = "GLWall shader wallpaper";
              wantedBy = [ "graphical-session.target" ];
              after = [ "graphical-session.target" ];
              serviceConfig = let
                powerArg = "--power-mode ${cfg.powerMode}";
                mouseArgs = "--mouse-overlay ${cfg.mouseOverlay} --mouse-overlay-height ${toString cfg.mouseOverlayHeight}";
                audioArgs = if cfg.audio.enable then
                  "--audio --audio-source ${cfg.audio.source}"
                else
                  "--no-audio --audio-source none";
                vertexArgs = lib.concatStringsSep " " (
                  (lib.optional cfg.shaders.allowVertex "--allow-vertex-shaders") ++
                  ["--vertex-count ${toString cfg.vertexCount}"]
                );
              in {
                ExecStart = "${cfg.package}/bin/glwall -s ${cfg.shaderPath} ${powerArg} ${mouseArgs} ${audioArgs} ${vertexArgs}";
                Restart = "on-failure";
              };
            };
          };
        };
    };
}
