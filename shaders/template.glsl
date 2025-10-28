// template.glsl
// GLWall Shader Template
//
// This template provides the basic structure for creating GLWall shaders.
// GLWall uses GLSL 330 core profile and provides ShaderToy-compatible uniforms.
//
// Usage:
// 1. Copy this template
// 2. Implement your mainImage() function
// 3. Run with: glwall -s your-shader.glsl

#version 330 core

// Output
out vec4 FragColor;

// Uniforms
uniform vec3 iResolution;     // Viewport resolution (width, height, aspect)
uniform float iTime;          // Time in seconds since start
uniform float iTimeDelta;     // Time since last frame (currently unused)
uniform float iBatteryLevel;  // Battery level 0.0-1.0 (currently unused)
uniform float iLocalTime;     // Local time 0.0-1.0 (currently unused)
uniform vec4 iMouse;          // Mouse position (currently unused)
uniform sampler2D myTexture;  // Texture input (currently unused)

// Main Image Function
//
// Implement your shader logic here. This function is called for each pixel.
// fragCoord contains the pixel coordinates (0,0) at bottom-left.
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Normalize coordinates to 0.0-1.0
    vec2 uv = fragCoord / iResolution.xy;
    
    // Example: Animated gradient
    vec3 color = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(iTime));
    
    fragColor = vec4(color, 1.0);
}

// Entry Point
//
// This function is called by OpenGL for each fragment.
// Do not modify unless you know what you're doing.
void main() {
    mainImage(FragColor, gl_FragCoord.xy);
}
