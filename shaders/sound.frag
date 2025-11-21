// AMOLED Sound Shader
// Frequency spectrum analyzer on pure black background

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    
    // Calculate frequency data from the sound texture
    // The sound texture usually contains frequency data in the first row (y=0)
    // and wave data in the second row (y=1).
    // We'll use frequency data for a spectrum analyzer look.
    
    float fft = texture(sound, vec2(uv.x, 0.25)).x; 
    
    // Create bars
    // Quantize x to create distinct bars
    float bars = 50.0;
    float bar_x = floor(uv.x * bars) / bars;
    float bar_fft = texture(sound, vec2(bar_x, 0.25)).x;
    
    // Bar height with some falloff
    float intensity = smoothstep(0.0, 0.01, bar_fft - uv.y);
    
    // Color based on x position (frequency)
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + uv.x * 3.0 + iTime);
    
    // Apply intensity and ensure black background
    vec3 finalColor = col * intensity;
    
    // Add a small glow at the top of the bars
    float glow = exp(-20.0 * abs(uv.y - bar_fft));
    finalColor += col * glow * 0.5;
    
    // Hard clamp to ensure deep blacks where there is no sound
    finalColor = max(finalColor, 0.0);
    
    // Vignette for style
    float vignette = 1.0 - length(uv - 0.5) * 0.5;
    finalColor *= vignette;

    fragColor = vec4(finalColor, 1.0);
}
