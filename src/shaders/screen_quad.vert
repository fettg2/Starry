#version 330 core
// 全屏四边形顶点着色器：
// 基本不做计算，只把裁剪空间坐标转换成 [0, 1] 纹理坐标。
layout (location = 0) in vec2 aPos;
out vec2 vUV;

void main() {
    // 屏幕空间 [-1, 1] 映射到纹理空间 [0, 1]。
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
