#version 330 core
// Pass 1 (canvas resolution): palette lookup + phosphor persistence.
// Output = max(current frame, previous output * decay): lit pixels fade out instead of vanishing.
out vec4 FragColor;

uniform usampler2D uCanvas;  // R8UI palette indices, row 0 = top of the canvas
uniform sampler2D uPalette;  // 16 x 1 RGB
uniform sampler2D uPrevious; // last frame's output (same size)
uniform float uDecay;        // 0 = no persistence
uniform ivec2 uCanvasSize;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    // Flip: the canvas is stored top-down, GL textures bottom-up.
    uint idx = texelFetch(uCanvas, ivec2(p.x, uCanvasSize.y - 1 - p.y), 0).r & 15u;
    vec3 color = texelFetch(uPalette, ivec2(int(idx), 0), 0).rgb;
    vec3 previous = texelFetch(uPrevious, p, 0).rgb * uDecay;
    FragColor = vec4(max(color, previous), 1.0);
}
