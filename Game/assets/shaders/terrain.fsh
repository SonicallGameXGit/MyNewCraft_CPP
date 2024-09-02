#version 410 core
#define DIFFUSE_AMBIENT_LIGHT 0.4

const vec3 SunDirection = normalize(vec3(-0.6, -1.0, 0.6));

layout(location = 0) in vec2 texcoord;
layout(location = 1) in vec3 normal;

layout(location = 2) in float ambient;
layout(location = 3) in vec3 vertexPosition;

uniform sampler2D colorSampler;
uniform vec3 eyePosition, fogColor;

void main() {
	gl_FragColor = texture2D(colorSampler, texcoord);
	gl_FragColor.rgb *= min(max(dot(-SunDirection, normalize(normal)), DIFFUSE_AMBIENT_LIGHT) * (DIFFUSE_AMBIENT_LIGHT + pow(ambient, 0.8) * (1.0 - DIFFUSE_AMBIENT_LIGHT)), 1.0);
	gl_FragColor.rgb = mix(gl_FragColor.rgb, fogColor, pow(smoothstep(length(vertexPosition.xz - eyePosition.xz) * 0.05, 0.0, 1.0), 2.0));
}