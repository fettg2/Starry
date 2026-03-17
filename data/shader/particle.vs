#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in float aSize;

out vec3 particleColor;
out float particleLife;
out float particleId;

uniform mat4 projection;
uniform mat4 view;
uniform float time;

void main()
{
    particleColor = aColor;
    particleLife = aSize;
    particleId = float(gl_VertexID);
    
    vec3 pos = aPos;
    float angle = time * 0.1 + length(aPos.xz) * 0.05;
    float s = sin(angle);
    float c = cos(angle);
    float x = pos.x * c - pos.z * s;
    float z = pos.x * s + pos.z * c;
    pos.x = x;
    pos.z = z;
    
    gl_Position = projection * view * vec4(pos, 1.0);
    gl_PointSize = aSize * (1.0 / gl_Position.w) * 600.0;
}
