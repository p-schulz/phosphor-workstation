#version 330 core
// Separable 9-tap Gaussian used for bloom (run once horizontally, once vertically).
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uImage;
uniform vec2 uDirection; // one texel step in uv, along x or y
uniform float uThreshold; // 0 = blur everything; >0 keeps only bright pixels (first pass)

vec3 fetch(vec2 uv) {
    vec3 c = texture(uImage, uv).rgb;
    float lum = max(max(c.r, c.g), c.b);
    return c * smoothstep(uThreshold, uThreshold + 0.25, lum);
}

void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 sum = fetch(vUV) * w[0];
    for (int i = 1; i < 5; ++i) {
        sum += fetch(vUV + uDirection * float(i)) * w[i];
        sum += fetch(vUV - uDirection * float(i)) * w[i];
    }
    FragColor = vec4(sum, 1.0);
}
