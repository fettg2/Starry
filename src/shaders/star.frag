#version 330 core
// 星点片元着色器：
// 把一个屏幕方块裁成圆形，再叠加多层高斯光晕和十字衍射。
in float vBrightness;
out vec4 FragColor;

void main() {
    // gl_PointCoord 是点精灵内部的局部坐标，范围 [0, 1]。
    vec2  p  = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(p, p);
    float r  = sqrt(r2);
    if (r2 > 1.0) discard;

    // 三层光晕：宽柔外晕 + 中层 + 尖锐核心
    float outerGlow = exp(-r2 *  1.2);
    float midGlow   = exp(-r2 *  7.0);
    float core      = exp(-r2 * 50.0);

    // 衍射十字光芒（模拟望远镜星星效果）
    float spike1 = exp(-p.x * p.x * 55.0) * exp(-r * 2.2);  // 垂直光芒
    float spike2 = exp(-p.y * p.y * 55.0) * exp(-r * 2.2);  // 水平光芒
    float spikes = max(spike1, spike2);

    // 颜色：暗星偏蓝白，亮星偏暖黄，核心和光芒会进一步拉向纯白。
    vec3 cold  = vec3(0.62, 0.80, 1.00);
    vec3 warm  = vec3(1.00, 0.83, 0.50);
    vec3 color = mix(cold, warm, vBrightness);
    color = mix(color, vec3(1.0), core * 0.88 + spikes * 0.30);

    // lum 是最终发光能量。这里不是物理正确模型，而是视觉优先的经验混合。
    float lum = outerGlow * 0.18
              + midGlow   * 0.55
              + core      * 3.50
              + spikes    * 0.85;
    lum *= (0.65 + vBrightness * 0.90);

    FragColor = vec4(color * lum, 1.0);
}
