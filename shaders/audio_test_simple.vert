// Sound-reactive waveform visualizer
uniform float time;

#define PI 3.14159265359

void main() {
    // Normalize vertex position (0 to 1)
    float freq = vertexId / vertexCount;
    
    // Sample audio at this frequency bin
    float audio = texture(sound, vec2(freq, 0.0)).r;
    
    // Boost audio response
    audio = pow(audio, 0.8);
    
    // X position: spread across screen (-1 to 1)
    float x = freq * 2.0 - 1.0;
    
    // Y position: waveform based on audio amplitude
    float y = audio * 1.5 - 0.75;
    
    // Add subtle wave animation
    y += sin(freq * PI * 4.0 + time * 2.0) * 0.05;
    
    gl_Position = vec4(x, y, 0.0, 1.0);
    gl_PointSize = 3.0 + audio * 8.0;
    
    // Color based on audio intensity
    // Low = blue, Mid = green, High = red/pink
    vec3 col = vec3(
        audio * 1.2,
        (1.0 - abs(audio - 0.5) * 2.0) * 0.8,
        1.0 - audio
    );
    v_color = vec4(col, 0.9);
}
