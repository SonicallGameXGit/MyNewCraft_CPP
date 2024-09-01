#version 410 core

#define DIFFUSE_AMBIENT_LIGHT 0.3
#define SUN_DIRECTION normalize(vec3(0.3, 1.0, 0.6))

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec3 normal;

uniform sampler2D colorSampler;

void main() {
	gl_FragColor = texture2D(colorSampler, texcoord);
	gl_FragColor.rgb *= max(dot(SUN_DIRECTION, normalize(normal)), DIFFUSE_AMBIENT_LIGHT);
}