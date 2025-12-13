uniform float time;

#define PI 3.14159265359

void main() {

    float freq = vertexId / vertexCount;

    float audio = texture(sound, vec2(freq, 0.0)).r;

    audio = pow(audio, 0.8);

    float x = freq * 2.0 - 1.0;

    float y = audio * 1.5 - 0.75;

    y += sin(freq * PI * 4.0 + time * 2.0) * 0.05;

    gl_Position = vec4(x, y, 0.0, 1.0);
    gl_PointSize = 3.0 + audio * 8.0;

    vec3 col = vec3(
        audio * 1.2,
        (1.0 - abs(audio - 0.5) * 2.0) * 0.8,
        1.0 - audio
    );
    v_color = vec4(col, 0.9);
}
