#pragma once

namespace FullscreenTriangle {

constexpr const char* vertexShader = R"(
#version 460

void main() {
	float x = float((gl_VertexID & 1) << 2) - 1.0;
	float y = float((gl_VertexID & 2) << 1) - 1.0;
	gl_Position = vec4(x, y, 0, 1);
}
)";

constexpr const char* fragmentShader = R"(
#version 460

uniform sampler2D texture0;

layout (location = 0) out vec4 outColor;

void main() {
	outColor = texelFetch(texture0, ivec2(gl_FragCoord.x, textureSize(texture0, 0).y - gl_FragCoord.y), 0);
}
)";

}

