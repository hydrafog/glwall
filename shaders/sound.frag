

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;

    float fft = texture(sound, vec2(uv.x, 0.25)).x;

    float bars = 50.0;
    float bar_x = floor(uv.x * bars) / bars;
    float bar_fft = texture(sound, vec2(bar_x, 0.25)).x;

    float intensity = smoothstep(0.0, 0.01, bar_fft - uv.y);

    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + uv.x * 3.0 + iTime);

    vec3 finalColor = col * intensity;

    float glow = exp(-20.0 * abs(uv.y - bar_fft));
    finalColor += col * glow * 0.5;

    finalColor = max(finalColor, 0.0);

    float vignette = 1.0 - length(uv - 0.5) * 0.5;
    finalColor *= vignette;

    fragColor = vec4(finalColor, 1.0);
}
