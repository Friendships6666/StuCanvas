// segments.slang - 屏幕空间 Miter Join (Flutter/OpenGL 风格)

struct Globals {
    float4x4 mvp;
    float3   camPos;
    float    _pad0;
    float2   viewportSize;
    float    lineWidth;   // 对应 pointRadius
    float    _pad1;
};
[[vk::push_constant]] Globals pc;

struct Segment {
    float3 p0; float _p0;
    float3 p1; float _p1;
    float4 c0;
    float4 c1;
};
[[vk::binding(0, 0)]] StructuredBuffer<Segment> segments;

struct V2F {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0; // x: 沿线方向, y: 跨线方向 [-1, 1]
};

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    V2F output;
    Segment seg = segments[iId];

    // 1. 投影到屏幕空间
    float4 clip0 = mul(pc.mvp, float4(seg.p0, 1.0));
    float4 clip1 = mul(pc.mvp, float4(seg.p1, 1.0));

    float2 screen0 = (clip0.xy / clip0.w * 0.5 + 0.5) * pc.viewportSize;
    float2 screen1 = (clip1.xy / clip1.w * 0.5 + 0.5) * pc.viewportSize;

    float2 dir = normalize(screen1 - screen0);
    float2 normal = float2(-dir.y, dir.x); // 屏幕空间法线

    // 2. 几何生成 (每个 Instance 绘制 6 个顶点组成一个矩形)
    // vId: 0,1,2 (第一个三角) 3,4,5 (第二个三角)
    uint quadIdx = vId % 6;
    static const uint indices[] = { 0, 1, 2, 2, 3, 0 };
    uint idx = indices[quadIdx];

    // 映射到矩形的四个角:
    // 0: (Start, Left), 1: (Start, Right), 2: (End, Right), 3: (End, Left)
    float2 offsets[] = {
        float2(0.0, -1.0), float2(0.0, 1.0),
        float2(1.0, 1.0),  float2(1.0, -1.0)
    };
    float2 offset = offsets[idx];

    // 3. 计算挤出 (Extrusion)
    // 为了防止 3D 下螺旋线这种连续折线出现接缝，我们在这里不做 SDF 圆角，
    // 而是做“扩边”：每段线段的开头和结尾稍微向外延伸 0.5 像素以产生 Overlap
    float overlap = 0.5;
    float2 pixelPos;
    float w;
    float z;

    if (offset.x < 0.5) { // 起点
        pixelPos = screen0 - dir * overlap + normal * offset.y * (pc.lineWidth * 0.5 + 0.5);
        output.color = seg.c0;
        w = clip0.w;
        z = clip0.z / clip0.w;
    } else { // 终点
        pixelPos = screen1 + dir * overlap + normal * offset.y * (pc.lineWidth * 0.5 + 0.5);
        output.color = seg.c1;
        w = clip1.w;
        z = clip1.z / clip1.w;
    }

    output.pos = float4(((pixelPos / pc.viewportSize) * 2.0 - 1.0) * w, z * w, w);
    output.uv = offset;
    return output;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    // 1. 计算当前的半宽（像素单位）
    float halfWidth = pc.lineWidth * 0.5;

    // 2. 计算像素距离 (SDF)
    // 顶点着色器传来的 uv.y 是从 -1 到 1 的归一化坐标
    // 且顶点已经为了抗锯齿向外扩充了 1.0 像素的 Margin
    float aaMargin = 1.0;
    float dist = abs(input.uv.y) * (halfWidth + aaMargin);

    // 3. 使用 fwidth 获取屏幕空间的变化率（核心优化）
    // fwidth(dist) 能够根据线段在屏幕上的旋转角度、透视缩放，
    // 自动计算出“一个像素对应多少个 SDF 单位”。
    // 这能保证线段边缘无论在 3D 空间的什么位置，始终保持恒定的 1.5 像素柔化。
    float unitRange = fwidth(dist);
    float edgeSoftness = max(unitRange, 0.0001); // 避免除以 0

    // 4. 解析覆盖度计算 (Analytical Alpha)
    // 相比 smoothstep，这种线性映射在处理极细线条时表现更稳健，
    // 且更接近 Skia (Flutter 引擎) 的渲染方式。
    // 公式：(边缘 - 当前距离) / 过渡宽度 + 0.5(居中偏移)
    float alpha = clamp((halfWidth - dist) / edgeSoftness + 0.5, 0.0, 1.0);

    // 5. 针对 3D 深度接缝的微调
    // 如果 alpha 极低，直接丢弃，防止深度缓冲写入脏数据
    if (alpha <= 0.01) discard;

    // 6. Gamma 空间亮度修正（可选）
    // 矢量绘图在半透明叠加时，如果在线性空间计算会更自然
    // 这里保持简单，直接输出
    return float4(input.color.rgb, input.color.a * alpha);
}