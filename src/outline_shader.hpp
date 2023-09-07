#pragma once

namespace OutlineShader {

constexpr const char* vertexShader = R"(
#version 460

layout (location = 0) uniform vec2 u_invScreenSize;
layout (location = 1) uniform vec4 u_extents;
layout (location = 2) uniform vec4 u_texCoords; // Maintained for compatibility
layout (location = 3) uniform vec4 u_color;

layout (location = 0) out vec4 v_color;

void main() {
	uint b = 1 << (gl_VertexID % 4);
	vec2 baseCoord = vec2((0x6 & b) != 0, (0xC & b) != 0);
	gl_Position = vec4(floor(fma(baseCoord, u_extents.zw, u_extents.xy)) * u_invScreenSize, 0, 1);
	gl_Position.xy = fma(gl_Position.xy, vec2(2.0, -2.0), vec2(-1.0, 1.0));
	v_color = u_color;
}
)";

constexpr const char* fragmentShader = R"(
#version 460

layout (location = 0) in vec4 v_color;

layout (location = 0) out vec4 outColor;

void main() {
	outColor = v_color;
}
)";

}

