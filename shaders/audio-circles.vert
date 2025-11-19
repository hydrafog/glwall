#version 330 core

// GLWall vertex shader: Audio-reactive circles visualization
// 
// This shader creates a grid of circles that react to audio input,
// using vertex ID-based geometry generation.

// Uniforms provided by GLWall
uniform vec3 iResolution;      // (width, height, aspect)
uniform float iTime;           // Time in seconds
uniform sampler2D sound;       // Audio texture
uniform float vertexCount;     // Total number of vertices

// Shader parameters
#define parameter0 15.0   // KParameter0
#define parameter1 5.0    // KParameter1
#define parameter2 1.5    // KParameter2
#define PI 3.14159265359

// Output to fragment shader
out vec4 v_color;

mat4 scale(float s) {
    return mat4(
        s, 0, 0, 0,
        0, s, 0, 0,
        0, 0, s, 0,
        0, 0, 0, 1
    );
}

void main() {
    float segmentsPerCircle = 16.0;
    float vertsPerSegment = 6.0;
    
    // Set base vert pos using gl_VertexID (GLSL built-in)
    float vertexId = float(gl_VertexID);
    float bx = mod(vertexId, 2.0) + floor(vertexId / 6.0);
    float by = mod(floor(vertexId / 2.0) + floor(vertexId / 3.0), 2.0);
    float angle = mod(bx, segmentsPerCircle) / segmentsPerCircle * 2.0 * PI;
    
    // Offset circles
    float circleCount = vertexCount / (segmentsPerCircle * vertsPerSegment);
    float circlesPerRow = 45.0;
    float circlesPerColumn = floor(circleCount / circlesPerRow);
    float circleId = floor(vertexId / (segmentsPerCircle * vertsPerSegment));
    float cx = mod(circleId, circlesPerRow);
    float cy = floor(circleId / circlesPerRow);
    
    float sx = cx - circlesPerRow * 0.5;
    float sy = cy - circlesPerColumn * 0.5;
    
    vec2 soundTexCoords0 = vec2(0.0, 0.0);
    float beatwave = (1.0 - abs(sin(iTime)) - 1.0) * sign(sin(iTime * 0.5));
    float sampleRange = beatwave * 0.025 + 0.125;
    soundTexCoords0.x = abs(atan(sx / sy)) / (PI * 0.5) * sampleRange;
    
    float maxRadius = sqrt(pow(circlesPerRow * 0.5, 2.0) + pow(circlesPerColumn * 0.5, 2.0));
    maxRadius *= 1.5;
    
    float historyDepth = 0.0625;
    float currentRadius = sqrt(pow(sx, 2.0) + pow(sy, 2.0)) / maxRadius;
    soundTexCoords0.y = currentRadius * historyDepth;
    
    vec2 soundTexCoords1 = soundTexCoords0;
    soundTexCoords1.y = historyDepth - soundTexCoords0.y + historyDepth;
    
    float outgoingR = texture(sound, soundTexCoords0).a;
    float r = outgoingR;
    r = r * (1.0 + soundTexCoords0.x) + 0.1 / parameter2;
    r = pow(r, 5.0);
    float radius = by * r;
    float x = cos(angle) * radius;
    float y = sin(angle) * radius;
    
    gl_Position = vec4(x, y, -r / 2.0, 1.0);
    gl_Position += vec4(cx - circlesPerRow * 0.5, cy - circlesPerColumn * 0.5, 0.0, 0.0);
    
    // Scale
    gl_Position *= scale((1.0 - r) / (6.0 + parameter1) / x * y);
    
    // Fix aspect ratio
    vec4 aspect = vec4(iResolution.y / iResolution.x, 1.0, 1.0, 1.0);
    gl_Position *= aspect;
    
    float g = (sin((abs(sx) + abs(sy)) * 0.25 - iTime * 8.0) * 0.5 + 0.5);
    v_color = vec4(r * 2.0 - 0.5, g, 1.0, 1.0);
    
    gl_PointSize = 4.0;
}
