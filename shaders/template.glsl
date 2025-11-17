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

// Uniforms (ShaderToy-like)
uniform vec3 iResolution;     // Viewport resolution (width, height, aspect)
uniform float iTime;          // Time in seconds since start
uniform float iTimeDelta;     // Time since last frame
uniform int   iFrame;         // Frame counter
uniform float iBatteryLevel;  // Battery level 0.0-1.0 (reserved)
uniform float iLocalTime;     // Local time 0.0-1.0 (reserved)
uniform vec4 iMouse;          // Mouse: (x, y, clickX, clickY) in pixels for this output
uniform sampler2D sound;      // Audio texture (mono strip): sample with soundRes
uniform vec2 soundRes;        // Resolution of `sound` texture (width, height)
uniform sampler2D myTexture;  // Example texture input (user-managed)

// Convenience aliases for porting shaders from other sites.
// Remove duplicate uniform declarations in your shader and use these names instead.
#define resolution iResolution.xy
#define time       iTime
#define mouse      iMouse.xy

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
