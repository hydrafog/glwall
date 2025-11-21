// AMOLED Normal Shader
// Pure black background with slowly moving neon curves

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    uv = uv * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;

    vec3 color = vec3(0.0);
    
    // Create a few moving lines
    for (float i = 0.0; i < 3.0; i++) {
        float t = iTime * 0.5 + i;
        
        // Curve calculation
        vec2 p = uv;
        p.x += sin(t * 0.5 + p.y * 1.5) * 0.5;
        p.y += cos(t * 0.3 + p.x * 0.5) * 0.3;
        
        // Distance to curve
        float d = abs(p.x + 0.1 * sin(p.y * 5.0 + t));
        
        // Neon glow effect (very thin line with falloff)
        float glow = 0.002 / max(d, 0.0001);
        glow = pow(glow, 1.5); // Sharpen the glow
        
        // Color palette (neon blue/purple/cyan)
        vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + i + t);
        
        color += col * glow;
    }
    
    // Ensure pure black background by clamping low values
    color = max(color - 0.05, 0.0);
    
    fragColor = vec4(color, 1.0);
}
