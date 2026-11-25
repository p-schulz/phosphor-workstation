#version 330 core
// Fullscreen triangle without a vertex buffer (ported from pvm-player's passthrough.vert).
// Draw with glDrawArrays(GL_TRIANGLES, 0, 3) and an empty VAO bound.
out vec2 vUV;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 pos = positions[gl_VertexID];
    vUV = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
