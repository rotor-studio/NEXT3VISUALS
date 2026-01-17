#version 150

uniform vec2 u_resolution;
uniform float u_time;
uniform float u_intensity;

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
    vec2 p = uv * vec2(2.0, 1.0);
    float t = u_time * 0.25;
    float v = 1.0 - uv.y;

    float flow = v * 2.0 + t;
    float waves = sin(flow * 7.5) * 0.12 + sin((p.x + t * 0.4) * 5.5) * 0.08;
    float n = fbm(p * 3.0 + vec2(0.0, t * 1.1));
    float foam = smoothstep(0.3, 0.6, n + waves);
    foam = pow(foam, 0.8);
    float mist = fbm(p * 1.2 + vec2(0.0, t * 0.2));

    float edge = smoothstep(0.0, 0.4, uv.x)
        * smoothstep(0.0, 0.4, 1.0 - uv.x)
        * smoothstep(0.0, 0.35, uv.y)
        * smoothstep(0.0, 0.45, 1.0 - uv.y);
    float bottomMask = smoothstep(0.02, 0.98, v);
    float crest = smoothstep(0.0, 0.35, uv.y);

    vec3 foamColor = mix(vec3(0.72, 0.82, 0.9), vec3(0.94, 0.97, 0.99), foam);
    foamColor = mix(foamColor, vec3(0.84, 0.9, 0.94), mist * 0.6);
    float coverage = clamp(foam * 1.4 + mist * 0.9, 0.0, 1.0);
    float alpha = pow(coverage, 0.75) * edge * bottomMask * (0.8 + crest * 0.4) * u_intensity;

    outputColor = vec4(foamColor, alpha);
}
