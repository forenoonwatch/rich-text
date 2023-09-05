#pragma once

namespace RectShader {

constexpr const char* vertexShader = R"(
#version 460

layout (location = 0) uniform vec2 u_invScreenSize;
layout (location = 1) uniform vec4 u_extents;
layout (location = 2) uniform vec4 u_texCoords;
layout (location = 3) uniform vec4 u_color;

layout (location = 0) out vec2 v_texCoord;
layout (location = 1) out vec4 v_color;

void main() {
	uint b = 1 << (gl_VertexID % 6);
	vec2 baseCoord = vec2((0x19 & b) != 0, (0xB & b) != 0);
	gl_Position = vec4(floor(fma(baseCoord, u_extents.zw, u_extents.xy)) * u_invScreenSize, 0, 1);
	gl_Position.xy = fma(gl_Position.xy, vec2(2.0, -2.0), vec2(-1.0, 1.0));
	v_texCoord = fma(baseCoord, u_texCoords.zw, u_texCoords.xy);
	v_color = u_color;
}
)";

constexpr const char* fragmentShader = R"(
#version 460

uniform sampler2D u_texture;

layout (location = 0) in vec2 v_texCoord;
layout (location = 1) in vec4 v_color;

layout (location = 0) out vec4 outColor;

void main() {
	outColor = texture(u_texture, v_texCoord) * v_color;
}
)";

}

