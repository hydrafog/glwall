#version 330 core

// GLWall fragment shader: Simple passthrough for vertex-generated colors
//
// This fragment shader simply outputs the color computed by the vertex shader.
// Use with the audio-circles.vert vertex shader.

in vec4 v_color;
out vec4 FragColor;

void main() {
    FragColor = v_color;
}
