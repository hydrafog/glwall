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
            wayland
            wayland-protocols
            wlr-protocols
            egl-wayland
          ];

          # Set protocol directories for Makefile
          WAYLAND_PROTOCOLS_DIR = "${pkgs.wayland-protocols}/share/wayland-protocols";
          WLR_PROTOCOLS_DIR = "${pkgs.wlr-protocols}/share/wlr-protocols";

          buildPhase = ''
            cd src
            make clean
            make LDFLAGS="-lGL -lGLEW -lEGL -lwayland-client -lwayland-egl -lm"
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp ./glwall $out/bin/
            mkdir -p $out/share/glwall/shaders
            cp -r ../shaders/* $out/share/glwall/shaders/
          '';

          meta = with pkgs.lib; {
            description = "A multi-monitor GLSL shader wallpaper renderer for Wayland";
            homepage = "https://github.com/yourusername/glwall";
            license = licenses.mit;
            maintainers = [ maintainers.hyperfog ];
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
          
          shellHook = ''
            export WAYLAND_PROTOCOLS_DIR="${pkgs.wayland-protocols}/share/wayland-protocols"
            export WLR_PROTOCOLS_DIR="${pkgs.wlr-protocols}/share/wlr-protocols"
            
            echo "GLWall development environment"
            echo "=============================="
            echo ""
            echo "Build commands:"
            echo "  cd src && make        # Build the project"
            echo "  cd src && make clean  # Clean build artifacts"
            echo ""
            echo "Run:"
            echo "  cd src && ./glwall -s ../shaders/retrowave.glsl --debug"
            echo ""
          '';
        };
      }
    ) // {
      # NixOS module
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
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ];
            
            # Optional: Add systemd user service for auto-start
            # systemd.user.services.glwall = {
            #   description = "GLWall shader wallpaper";
            #   wantedBy = [ "graphical-session.target" ];
            #   serviceConfig = {
            #     ExecStart = "${cfg.package}/bin/glwall -s ${cfg.shaderPath}";
            #     Restart = "on-failure";
            #   };
            # };
          };
        };
    };
}
