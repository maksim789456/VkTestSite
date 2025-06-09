#version 450

vec2 pos[3] = vec2[](
  vec2(0.0, -0.5),
  vec2(0.5, 0.5),
  vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
  vec3(1.0, 0.0, 0.0),
  vec3(0.0, 1.0, 0.0),
  vec3(0.0, 0.0, 1.0)
);

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;
layout (location = 3) in vec4 inColor;
layout (location = 4) in uint inTexIdx;

layout(location = 0) out vec3 fragColor;

void main() {
  gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
  fragColor = colors[gl_VertexIndex];
}