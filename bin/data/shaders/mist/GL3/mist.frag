#version 150

uniform vec2 u_resolution;
uniform float u_time;
uniform float u_intensity;
uniform float u_speed;

out vec4 outputColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amp * noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return value;
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    float t = u_time * 0.35 * u_speed;
    vec2 flow = vec2(uv.x * 2.0, uv.y * 3.0 - t);
    float n = fbm(flow + vec2(0.0, -t * 0.4));
    float streaks = fbm(flow * vec2(1.0, 2.5) + vec2(0.0, -t * 0.9));
    float mist = smoothstep(0.25, 0.85, n) * 0.6 + smoothstep(0.3, 0.9, streaks) * 0.5;

    float edge = smoothstep(0.0, 0.35, uv.x) * smoothstep(0.0, 0.35, 1.0 - uv.x);
    float topFade = smoothstep(0.0, 0.2, uv.y);
    float alpha = mist * edge * topFade * u_intensity;
    vec3 color = mix(vec3(0.72, 0.82, 0.9), vec3(0.9, 0.95, 0.98), mist);

    outputColor = vec4(color, alpha);
}
