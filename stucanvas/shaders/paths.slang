// paths.slang - 终极稳健版 (3D高密度细分 + Segment SDF)

struct Globals {
    float4x4 mvp;           // Offset 0, Size 64
    float3   camPos;        // Offset 64, Size 12
    float    _pad0;         // Offset 76, Size 4
    float2   viewportSize;  // Offset 80, Size 8
    float    pointRadius;   // Offset 88, Size 4
    float    _pad1;         // Offset 92, Size 4
};
[[vk::push_constant]] Globals pc;

struct ControlPoint {
    float4 pos_pad;
    float4 color;
};
[[vk::binding(0, 0)]] StructuredBuffer<ControlPoint> allPoints;

struct V2F {
    float4 pos : SV_Position;
    float2 pixelPos : TEXCOORD0;
    nointerpolation uint instanceId : INST_ID;
};

// 1. 获取 3D 空间中的贝塞尔曲线点
float3 getPos3D(float3 p0, float3 p1, float3 p2, float3 p3, float t) {
    float u = 1.0 - t;
    float uu = u * u;
    float tt = t * t;
    return (uu * u) * p0 + (3.0 * uu * t) * p1 + (3.0 * u * tt) * p2 + (tt * t) * p3;
}

// 2. 线段的解析距离公式 (绝对稳健，永不断裂)
float sdSegment(float2 p, float2 a, float2 b, out float h) {
    float2 pa = p - a, ba = b - a;
    float baba = dot(ba, ba);
    // 防止除以 0
    h = (baba > 1e-6) ? clamp(dot(pa, ba) / baba, 0.0, 1.0) : 0.0;
    return length(pa - ba * h);
}

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    V2F output;
    uint base = iId * 3;

    float3 p0 = allPoints[base+0].pos_pad.xyz;
    float3 p1 = allPoints[base+1].pos_pad.xyz;
    float3 p2 = allPoints[base+2].pos_pad.xyz;
    float3 p3 = allPoints[base+3].pos_pad.xyz;

    float2 minPx = pc.viewportSize;
    float2 maxPx = float2(0.0, 0.0);
    bool visible = false;

    // 严密计算屏幕上的真实包围盒
    // 通过在 3D 空间采样 16 次，完美解决控制点在相机后面的爆炸问题
    const int AABB_SAMPLES = 16;
    for (int i = 0; i <= AABB_SAMPLES; ++i) {
        float t = float(i) / float(AABB_SAMPLES);
        float3 pos3D = getPos3D(p0, p1, p2, p3, t);
        float4 clip = mul(pc.mvp, float4(pos3D, 1.0));

        // 只保留位于相机前方的片段
        if (clip.w > 0.01) {
            float2 px = (clip.xy / clip.w * 0.5 + 0.5) * pc.viewportSize;
            minPx = min(minPx, px);
            maxPx = max(maxPx, px);
            visible = true;
        }
    }

    // 如果曲线完全在相机后面，丢弃该顶点
    if (!visible) {
        output.pos = float4(0, 0, 0, 0);
        output.pixelPos = float2(0, 0);
        output.instanceId = iId;
        return output;
    }

    // 加上粗细和抗锯齿边缘
    float padding = pc.pointRadius * 0.5 + 4.0;
    minPx -= padding;
    maxPx += padding;

    static const float2 kCorners[4] = { float2(-1,-1), float2(1,-1), float2(1,1), float2(-1,1) };
    static const uint kIndices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 cornerPx = lerp(minPx, maxPx, kCorners[kIndices[vId % 6]] * 0.5 + 0.5);

    output.pos = float4((cornerPx / pc.viewportSize) * 2.0 - 1.0, 0.0, 1.0);
    output.pixelPos = cornerPx;
    output.instanceId = iId;
    return output;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    uint base = input.instanceId * 3;
    float3 p0 = allPoints[base+0].pos_pad.xyz;
    float3 p1 = allPoints[base+1].pos_pad.xyz;
    float3 p2 = allPoints[base+2].pos_pad.xyz;
    float3 p3 = allPoints[base+3].pos_pad.xyz;

    float2 q = input.pixelPos;

    // --- 核心：高密度连贯线段距离求解 ---
    // 48 段细分对于任何弯曲的曲线都足够实现完美的视觉平滑
    const int STEPS = 128;
    float minDist = 1e10;
    float bestT = 0.0;

    // 获取第 0 个点
    float3 lastPos3D = getPos3D(p0, p1, p2, p3, 0.0);
    float4 lastClip = mul(pc.mvp, float4(lastPos3D, 1.0));
    float2 lastPx = (lastClip.xy / lastClip.w * 0.5 + 0.5) * pc.viewportSize;
    bool lastValid = (lastClip.w > 0.01);

    // 遍历曲线上的 48 个小段
    for (int i = 1; i <= STEPS; ++i) {
        float t = float(i) / float(STEPS);
        float3 currPos3D = getPos3D(p0, p1, p2, p3, t);
        float4 currClip = mul(pc.mvp, float4(currPos3D, 1.0));
        bool currValid = (currClip.w > 0.01);

        // 如果该线段的起点和终点都在相机前面
        if (currValid && lastValid) {
            float2 currPx = (currClip.xy / currClip.w * 0.5 + 0.5) * pc.viewportSize;

            float h; // h 是投影在线段上的相对比例 [0, 1]
            float d = sdSegment(q, lastPx, currPx, h);

            if (d < minDist) {
                minDist = d;
                // 计算精确的 t (当前段起始 t 加上线段内部的偏移 h)
                bestT = (float(i - 1) + h) / float(STEPS);
            }
            lastPx = currPx;
        }
        else if (currValid) {
            // 如果上一个点在背后，当前点跑到前面来了，重新记录起点
            lastPx = (currClip.xy / currClip.w * 0.5 + 0.5) * pc.viewportSize;
        }
        lastValid = currValid;
    }

    float halfWidth = pc.pointRadius * 0.5;

    // 绝对裁剪：防止在包围盒角落渲染垃圾数据
    if (minDist > halfWidth + 1.5) discard;

    // 抗锯齿
    float alpha = smoothstep(halfWidth + 0.5, halfWidth - 0.5, minDist);
    if (alpha <= 0.0) discard;

    // 颜色插值
    float4 c0 = allPoints[base+0].color;
    float4 c1 = allPoints[base+1].color;
    float4 c2 = allPoints[base+2].color;
    float4 c3 = allPoints[base+3].color;

    float u = 1.0 - bestT;
    float4 color = (u*u*u)*c0 + (3.0*u*u*bestT)*c1 + (3.0*u*bestT*bestT)*c2 + (bestT*bestT*bestT)*c3;

    return float4(color.rgb, color.a * alpha);
}