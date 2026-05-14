// triangles.slang - 显式相机位置 + 实时面法线计算 + 双面着色

struct Globals {
    float4x4 mvp;           // 偏移 0
    float3 camPos;          // 偏移 64
    float  _pad0;           // 偏移 76
    float2 viewportSize;    // 偏移 80
    float  pointRadius;     // 偏移 88 (即 C++ 里的 strokeWidth/pointRadius)
    float  _pad1;           // 偏移 92
};

[[vk::push_constant]] Globals pc;

// 输入结构匹配 C++ PointDataGPU (32字节)
struct VSInput {
    [[vk::location(0)]] float3 pos   : POSITION;
    [[vk::location(1)]] float  _pad;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct V2F {
    float4 clipPos  : SV_Position;
    float3 worldPos : TEXCOORD0;
    float4 color    : COLOR;
};

[shader("vertex")]
V2F vertexMain(VSInput input) {
    V2F output;

    // 使用传入的 MVP 矩阵进行变换
    output.clipPos = mul(pc.mvp, float4(input.pos, 1.0));

    // 传递归一化的世界空间坐标
    output.worldPos = input.pos;
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    // --- A. 利用屏幕空间变化率计算面法线 (Flat Shading) ---
    // 由于没有传入顶点法线，通过片元间的世界坐标梯度叉积得到
    float3 dX = ddx(input.worldPos);
    float3 dY = ddy(input.worldPos);
    float3 faceNormal = normalize(cross(dX, dY));

    // --- B. 计算精确的视线方向 (禁止简化) ---
    // 使用从 PushConstants 传入的相机位置减去当前片元的世界位置
    float3 viewDir = normalize(pc.camPos - input.worldPos);

    // --- C. 双面着色逻辑 ---
    // 如果计算出的面法线背对相机 (N·V < 0)，则翻转法线
    // 确保无论观察面是物体的“内”还是“外”，光照方向始终是一致的受光面
    if (dot(faceNormal, viewDir) < 0.0) {
        faceNormal = -faceNormal;
    }

    // --- D. 硬编码光照设定 ---
    // 光源位置同样位于归一化空间附近
    const float3 lightPos = float3(5.0, 5.0, 10.0);
    const float3 lightColor = float3(1.0, 1.0, 1.0);
    const float  lightIntensity = 0.9;
    const float  ambientStrength = 0.25;

    // --- E. 经典 Lambert 光照计算 ---
    float3 lightDir = normalize(lightPos - input.worldPos);

    // 漫反射
    float diff = max(dot(faceNormal, lightDir), 0.0);
    float3 diffuse = diff * lightColor * lightIntensity;

    // 环境光 (基于基础色，确保阴影处不全黑)
    float3 ambient = ambientStrength * lightColor;

    // 最终颜色合成：(环境光 + 漫反射) * 顶点颜色
    float3 finalRGB = (ambient + diffuse) * input.color.rgb;

    return float4(finalRGB, input.color.a);
}