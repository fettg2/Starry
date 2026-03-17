#version 330 core
// 星云后处理片元着色器：
// 对屏幕上的每个像素反推一条视线，在该方向上采样程序化噪声，生成体积感星云。
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uForeground;
uniform sampler2D uStars;
uniform vec2 uResolution;
uniform float uTime;
uniform float uNebulaScale;
uniform float uNebulaDensity;
uniform float uNebulaSpeed;
uniform float uNebulaContrast;
uniform float uNebulaBrightness;
uniform float uStarBoost;
uniform vec3 uNebulaColorA;
uniform vec3 uNebulaColorB;
uniform vec3 uNebulaColorC;
uniform vec3 uCamPos;
uniform vec3 uCamFront;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uAspect;
uniform float uTanHalfFov;

float hash31(vec3 p) {
    // 轻量级哈希：把整数格点映射到稳定伪随机数。
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise3(vec3 p) {
    // 标准 value noise：对立方体 8 个角的随机值做三线性插值。
    vec3 i = floor(p);
    vec3 f = fract(p);
    // smoothstep 形式的插值权重，让格点边界更平滑。
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

float fbm(vec3 p) {
    // FBM = 多层不同频率噪声叠加，是很多云雾/地形效果的基础。
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * valueNoise3(p);
        // 每一层频率更高、振幅更小，形成“大形状 + 小细节”。
        p = p * 2.03 + vec3(1.7, -1.3, 0.8);
        amp *= 0.5;
    }
    return sum;
}

void main() {
    // 先把屏幕 UV 转成以相机为原点的一条视线方向。
    vec2 ndc = vUV * 2.0 - 1.0;
    ndc.x *= uAspect;
    vec3 rayDir = normalize(uCamFront + ndc.x * uTanHalfFov * uCamRight + ndc.y * uTanHalfFov * uCamUp);

    // p 是噪声采样坐标：包含视线方向、相机位置偏移和时间漂移。
    vec3 drift = vec3(0.0, uTime * uNebulaSpeed, uTime * uNebulaSpeed * 0.4);
    vec3 p = rayDir * uNebulaScale + uCamPos * 0.0045 + drift;

    // 三组不同频率的 FBM 负责大轮廓、中尺度变化和高频尘埃。
    float n0 = fbm(p * 1.45);
    float n1 = fbm(p * 2.60 + vec3(12.3, -4.2, 8.1));
    float n2 = fbm(p * 5.0 + vec3(-7.1, 5.6, -2.3));

    // 额外叠一条“银河带”，让星云更像沿某个天区聚集，而不是平均铺满天空。
    vec3 galacticPlane = normalize(vec3(0.12, 1.0, 0.05));
    float band = exp(-pow(abs(dot(rayDir, galacticPlane)) * 4.0, 1.5));

    // cloud 决定主云层厚度，dust 决定细碎遮挡感。
    float cloud = clamp(n0 * 0.8 + n1 * 0.5 + band * 0.7 - 0.3, 0.0, 1.0);
    cloud = pow(cloud, uNebulaContrast);
    float dust = smoothstep(0.25, 0.95, n2);

    // 颜色不是直接查纹理，而是按噪声强度在三组颜色之间渐变。
    vec3 nebulaColor = mix(uNebulaColorA, uNebulaColorB, clamp(n0 * 1.2, 0.0, 1.0));
    nebulaColor = mix(nebulaColor, uNebulaColorC, clamp(n1 * 1.4, 0.0, 1.0));

    float density = clamp(cloud * uNebulaDensity, 0.0, 2.0);
    vec3 nebula = nebulaColor * density * uNebulaBrightness;
    nebula *= mix(0.45, 1.0, dust);

    // 前景层已经单独渲染成带 alpha 的颜色图。
    vec4 foregroundColor = texture(uForeground, vUV);
    vec3 starColor = texture(uStars, vUV).rgb * uStarBoost;
    vec3 backgroundColor = starColor + nebula;
    vec3 col = mix(backgroundColor, foregroundColor.rgb, clamp(foregroundColor.a, 0.0, 1.0));
    // 简化的 tone mapping，防止亮部直接截断到 1.0。
    col = 1.0 - exp(-col);
    FragColor = vec4(col, 1.0);
}
