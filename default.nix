{ ... }:

{
  imports = [ ./module.nix ];

  glwall = {
    enable = true;
    shaderPath = builtins.toString ../shaders/retrowave.glsl;
    imagePath = null;
  };
}
