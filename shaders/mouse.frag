// AMOLED Mouse Shader
// Interactive trail following the mouse on pure black background

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    uv.x *= iResolution.x / iResolution.y;
    
    vec2 mouse = iMouse.xy / iResolution.xy;
    mouse.x *= iResolution.x / iResolution.y;
    
    // Distance from mouse
    float d = length(uv - mouse);
    
    // Create a glowing orb at the mouse position
    float glow = 0.01 / (d * d + 0.001);
    glow = clamp(glow, 0.0, 1.0);
    
    // Add a ripple effect based on time and distance
    float ripple = sin(d * 50.0 - iTime * 5.0) * 0.5 + 0.5;
    float rippleIntensity = smoothstep(0.5, 0.0, d); // Fade out ripple quickly
    
    // Combine glow and ripple
    vec3 col = vec3(0.2, 0.5, 1.0) * glow;
    col += vec3(0.5, 0.2, 1.0) * ripple * rippleIntensity;
    
    // Add some background particles or faint grid for context (optional, keeping it minimal for AMOLED)
    // Just a very faint grid
    float grid = step(0.98, fract(uv.x * 20.0)) + step(0.98, fract(uv.y * 20.0));
    col += vec3(0.1) * grid * (1.0 - min(d * 2.0, 1.0)); // Grid fades out near mouse
    
    // Ensure black background
    col = max(col, 0.0);
    
    fragColor = vec4(col, 1.0);
}
