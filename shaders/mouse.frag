uniform vec3 iResolution;
uniform vec4 iMouse;
uniform float iTime;

void main() {
    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    uv.x *= iResolution.x / iResolution.y;

    vec2 mouse = iMouse.xy / iResolution.xy;
    mouse.x *= iResolution.x / iResolution.y;

    float d = length(uv - mouse);

    float glow = 0.01 / (d * d + 0.001);
    glow = clamp(glow, 0.0, 1.0);

    float ripple = sin(d * 50.0 - iTime * 5.0) * 0.5 + 0.5;
    float rippleIntensity = smoothstep(0.5, 0.0, d);

    vec3 col = vec3(0.2, 0.5, 1.0) * glow;
    col += vec3(0.5, 0.2, 1.0) * ripple * rippleIntensity;

    float grid = step(0.98, fract(uv.x * 20.0)) + step(0.98, fract(uv.y * 20.0));
    col += vec3(0.1) * grid * (1.0 - min(d * 2.0, 1.0));

    col = max(col, 0.0);

    fragColor = vec4(col, 1.0);
}
