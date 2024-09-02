#version 410 core

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec3 normal;
layout(location = 3) in float ambient;

uniform mat4 mvpMatrix, modelMatrix;

layout(location = 0) out vec2 _texcoord;
layout(location = 1) out vec3 _normal;

layout(location = 2) out float _ambient;
layout(location = 3) out vec3 _vertexPosition;

void main() {
    gl_Position = mvpMatrix * position;
    
    _texcoord = texcoord;
    _normal = (modelMatrix * vec4(normal, 0.0)).xyz;
    _ambient = 1.0 - ambient / 3.0;
    _vertexPosition = (modelMatrix * position).xyz;
}