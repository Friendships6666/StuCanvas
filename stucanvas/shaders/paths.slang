// paths.slang - 基于牛顿法精确求解距离

struct Globals {
    float4x4 mvp;
    float2 viewportSize;
    float strokeWidth;
};
[[vk::push_constant]] Globals pc;

struct ControlPoint {
    float3 pos;
    float  _pad;
    float4 color;
};

[[vk::binding(0, 0)]] StructuredBuffer<ControlPoint> allPoints;

struct V2F {
    float4 pos : SV_Position;
    float2 pixelPos : TEXCOORD0;
    nointerpolation uint instanceId : INST_ID;
};

// --- 数学辅助函数 ---

// 三次贝塞尔位置 B(t)
float2 getPos(float2 p0, float2 p1, float2 p2, float2 p3, float t) {
    float u = 1.0 - t;
    return u*u*u*p0 + 3.0*u*u*t*p1 + 3.0*u*t*t*p2 + t*t*t*p3;
}

// 一阶导 B'(t)
float2 getDeriv1(float2 p0, float2 p1, float2 p2, float2 p3, float t) {
    float u = 1.0 - t;
    return 3.0*u*u*(p1 - p0) + 6.0*u*t*(p2 - p1) + 3.0*t*t*(p3 - p2);
}

// 二阶导 B''(t)
float2 getDeriv2(float2 p0, float2 p1, float2 p2, float2 p3, float t) {
    return 6.0*(1.0 - t)*(p2 - 2.0*p1 + p0) + 6.0*t*(p3 - 2.0*p2 + p1);
}

// 投影函数
float2 projectToPixel(float3 pos) {
    float4 clip = mul(pc.mvp, float4(pos, 1.0));
    return (clip.xy / clip.w * 0.5 + 0.5) * pc.viewportSize;
}

// --- Shader 阶段 ---

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    V2F output;
    static const float2 kCorners[4] = { float2(-1,-1), float2(1,-1), float2(1,1), float2(-1,1) };
    static const uint kIndices[6] = { 0, 1, 2, 0, 2, 3 };

    uint base = iId * 3;
    float2 px[4];
    for(int i=0; i<4; ++i) px[i] = projectToPixel(allPoints[base+i].pos);

    float2 minPx = min(min(px[0], px[1]), min(px[2], px[3]));
    float2 maxPx = max(max(px[0], px[1]), max(px[2], px[3]));

    float padding = pc.strokeWidth * 0.5 + 2.0;
    minPx -= padding; maxPx += padding;

    uint cornerIdx = kIndices[vId % 6];
    float2 cornerPx = lerp(minPx, maxPx, kCorners[cornerIdx] * 0.5 + 0.5);

    output.pos = float4((cornerPx / pc.viewportSize) * 2.0 - 1.0, 0.0, 1.0);
    output.pixelPos = cornerPx;
    output.instanceId = iId;
    return output;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    uint base = input.instanceId * 3;
    float2 p0 = projectToPixel(allPoints[base+0].pos);
    float2 p1 = projectToPixel(allPoints[base+1].pos);
    float2 p2 = projectToPixel(allPoints[base+2].pos);
    float2 p3 = projectToPixel(allPoints[base+3].pos);
    float2 q  = input.pixelPos;

    // 1. 粗略搜索 (Coarse Search) - 找到牛顿法的起点
    // 使用 8 个采样点
    const int SAMPLES = 8;
    float bestT = 0.0;
    float minDistSq = 1e10;
    for (int i = 0; i <= SAMPLES; ++i) {
        float t = float(i) / float(SAMPLES);
        float2 pos = getPos(p0, p1, p2, p3, t);
        float d2 = dot(pos - q, pos - q);
        if (d2 < minDistSq) {
            minDistSq = d2;
            bestT = t;
        }
    }

    // 2. 牛顿迭代 (Newton Iteration)
    // 迭代 4 次通常足以达到像素级精度
    float currT = bestT;
    for (int j = 0; j < 4; ++j) {
        float2 B = getPos(p0, p1, p2, p3, currT);
        float2 D1 = getDeriv1(p0, p1, p2, p3, currT);
        float2 D2 = getDeriv2(p0, p1, p2, p3, currT);

        float2 delta = B - q;

        // f'(t) = 2 * (B(t) - Q) . B'(t)
        float f1 = dot(delta, D1);
        // f''(t) = 2 * (B'(t)^2 + (B(t) - Q) . B''(t))
        float f2 = dot(D1, D1) + dot(delta, D2);

        if (abs(f2) < 1e-6) break; // 防止除零

        float nextT = currT - f1 / f2;
        currT = clamp(nextT, 0.0, 1.0); // 必须限制在 [0, 1] 空间内
    }

    // 计算最终位置和距离
    float2 finalPos = getPos(p0, p1, p2, p3, currT);
    float dist = length(finalPos - q);

    // 3. 渲染
    float halfWidth = pc.strokeWidth * 0.5;
    float alpha = smoothstep(halfWidth + 0.75, halfWidth - 0.75, dist);

    if (alpha <= 0.0) discard;

    // 颜色插值
    float4 c0 = allPoints[base+0].color;
    float4 c1 = allPoints[base+1].color;
    float4 c2 = allPoints[base+2].color;
    float4 c3 = allPoints[base+3].color;

    // 使用贝塞尔基函数插值颜色
    float u = 1.0 - currT;
    float4 color = u*u*u*c0 + 3.0*u*u*currT*c1 + 3.0*u*currT*currT*c2 + currT*currT*currT*c3;

    return float4(color.rgb, color.a * alpha);
}