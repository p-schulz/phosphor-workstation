#version 330 core
// Final pass into the letterboxed viewport: sharp upscale, scanline beam profile, aperture mask,
// bloom, vignette and optional curvature. Structure follows pvm-player's crt.frag, extended for an
// indexed low-resolution canvas.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uImage; // phosphor pass output, canvas resolution, linear filtering
uniform sampler2D uBloom; // blurred copy, half resolution
uniform vec2 uCanvasSize;
uniform vec2 uOutputSize; // viewport size in framebuffer pixels
uniform int uEnabled;     // 0 = plain sharp upscale only
uniform float uScanlines;
uniform float uMask;
uniform float uBloomAmount;
uniform float uVignette;
uniform float uCurvature;
uniform float uBrightness;

// Must match gfx::crt_distort() (used to map the light pen).
vec2 distort(vec2 uv) {
    if (uCurvature <= 0.0) {
        return uv;
    }
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + uCurvature * 0.25 * r2);
}

// Sharp-bilinear: nearest-neighbour look with a one-pixel blend at texel edges, so non-integer
// scale factors do not produce uneven pixel widths.
vec2 sharp(vec2 uv, vec2 size, vec2 scale) {
    vec2 texel = uv * size;
    vec2 base = floor(texel);
    vec2 d = fract(texel) - 0.5;
    vec2 range = 0.5 - 0.5 / max(scale, vec2(1.0));
    vec2 f = (d - clamp(d, -range, range)) * max(scale, vec2(1.0)) + 0.5;
    return (base + f) / size;
}

void main() {
    vec2 uv = distort(vUV);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 scale = uOutputSize / uCanvasSize;
    if (uEnabled == 0) {
        FragColor = vec4(texture(uImage, sharp(uv, uCanvasSize, scale)).rgb, 1.0);
        return;
    }

    // Sample at the centre of the canvas row; the beam profile then shapes the row vertically.
    float row_pos = uv.y * uCanvasSize.y;
    float row_center = (floor(row_pos) + 0.5) / uCanvasSize.y;
    vec2 suv = sharp(vec2(uv.x, row_center), uCanvasSize, vec2(scale.x, 1.0));
    vec3 color = texture(uImage, suv).rgb;

    // Beam: brighter pixels make wider lines, as on a real tube. Fade the effect out when the
    // output has fewer than ~2 pixels per canvas row, where scanlines would only alias.
    float lum = max(max(color.r, color.g), color.b);
    float dy = fract(row_pos) - 0.5;
    float sigma = mix(0.24, 0.42, lum);
    float beam = exp(-(dy * dy) / (2.0 * sigma * sigma));
    float strength = uScanlines * clamp(scale.y - 1.5, 0.0, 1.0);
    color *= mix(1.0, beam * 1.35, strength);

    // Aperture grille: a fine vertical texture every third output pixel.
    float grille = mod(gl_FragCoord.x, 3.0) < 1.0 ? 1.0 - uMask * 0.6 : 1.0;
    color *= grille;

    color += texture(uBloom, uv).rgb * uBloomAmount * 1.2;

    vec2 c = uv - 0.5;
    color *= clamp(1.0 - dot(c, c) * uVignette * 2.0, 0.0, 1.0);
    color *= 1.0 + uBrightness;

    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
