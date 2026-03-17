#version 330 core
// 星点顶点着色器：
// 输入一颗星的方向与亮度，输出它在屏幕上的位置和点精灵尺寸。
layout (location = 0) in vec4 aStar;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uPointSize;
uniform float uSpaceScale;
uniform float uTime;
uniform float uStarSpeed;
out float vBrightness;

void main() {
    // 取星星方向并保留半径
    float radius  = length(aStar.xyz) * uSpaceScale;
    vec3  dir     = normalize(aStar.xyz);

    // 星星自身旋转：绕 Y 轴匀速转圈，循环不跑偏
    float angle = uTime * uStarSpeed;
    float c = cos(angle), s = sin(angle);
    mat3  rot = mat3(
         c,   0.0,  s,
         0.0, 1.0,  0.0,
        -s,   0.0,  c
    );
    dir = rot * dir;

    // 世界空间中的星点位置。这里不做平移动画，而是整体转动方向。
    vec3  worldPos = dir * radius;

    // 天穹渲染：剥离相机平移，星星永远覆盖整个天穹，不会因镜头移动而跑出视野
    mat4 skyView  = uView;
    skyView[3]    = vec4(0.0, 0.0, 0.0, 1.0);

    vec4 viewPos  = skyView * vec4(worldPos, 1.0);
    gl_Position   = uProj * viewPos;

    // 距离越远点越小，但保留上下限，避免极近或极远时过于夸张。
    float dist    = max(length(viewPos.xyz), 1.0);
    float size    = uPointSize * (1200.0 / dist);
    gl_PointSize  = clamp(size, 1.5, 22.0);
    // 亮度直接透传到片元着色器，控制星星冷暖色和发光强度。
    vBrightness   = aStar.w;
}
