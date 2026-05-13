struct Globals {
    float4x4 mvp;
    float2 viewportSize;
    float lineWidth;
};
[[vk::push_constant]] Globals pc;

// 1. 内存完全匹配 C++ 的 SegmentGPU
struct Segment {
    float3 startPos;
    float  _pad0;
    float3 endPos;
    float  _pad1;
    float4 startColor;
    float4 endColor;
};
[[vk::binding(0, 0)]] StructuredBuffer<Segment> segments;

// 2. V2F 数据传输
struct V2F {
    float4 pos : SV_Position;
    float2 localPos : TEXCOORD0;
    float halfLen_pixel : TEXCOORD1;
    float4 startColor : COLOR0;
    float4 endColor : COLOR1;
};

static const float2 kRectCorners[4] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0),
    float2( 1.0,  1.0), float2(-1.0,  1.0)
};
static const uint kTriIndices[6] = { 0, 1, 2, 0, 2, 3 };

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    V2F outVert;
    uint triIndex = vId % 6;
    uint corner = kTriIndices[triIndex];
    Segment seg = segments[iId];

    float4 p0 = mul(pc.mvp, float4(seg.startPos, 1.0));
    float4 p1 = mul(pc.mvp, float4(seg.endPos, 1.0));

    float2 s0_ndc = p0.xy / p0.w;
    float2 s1_ndc = p1.xy / p1.w;

    float2 s0_pixel = (s0_ndc * 0.5 + 0.5) * pc.viewportSize;
    float2 s1_pixel = (s1_ndc * 0.5 + 0.5) * pc.viewportSize;

    float2 dir = s1_pixel - s0_pixel;
    float len_pixel = length(dir);
    float2 unitDir = (len_pixel > 0.0001) ? (dir / len_pixel) : float2(1.0, 0.0);
    float2 unitNormal = float2(-unitDir.y, unitDir.x);

    float halfW = pc.lineWidth * 0.5;
    float padding = halfW + 1.5;

    float halfLen = len_pixel * 0.5;
    float boxHalfLen = halfLen + padding;
    float boxHalfW = padding;

    float2 localCorner = kRectCorners[corner];
    float localX = localCorner.x * boxHalfLen;
    float localY = localCorner.y * boxHalfW;

    float2 mid_pixel = (s0_pixel + s1_pixel) * 0.5;
    float2 final_pixel = mid_pixel + unitDir * localX + unitNormal * localY;

    float2 final_ndc = (final_pixel / pc.viewportSize) * 2.0 - 1.0;

    float depth = (p0.z / p0.w + p1.z / p1.w) * 0.5;
    float w = (p0.w + p1.w) * 0.5;

    outVert.pos = float4(final_ndc * w, depth * w, w);
    outVert.localPos = float2(localX, localY);
    outVert.halfLen_pixel = halfLen;

    // 传递两个颜色
    outVert.startColor = seg.startColor;
    outVert.endColor = seg.endColor;
    return outVert;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    // --- 1. SDF 胶囊体轮廓计算 ---
    float dx = max(abs(input.localPos.x) - input.halfLen_pixel, 0.0);
    float dy = input.localPos.y;
    float distToLine = length(float2(dx, dy));
    float dist = distToLine - (pc.lineWidth * 0.5);

    float alpha = smoothstep(1.5, 0.0, dist);
    if (alpha <= 0.0) discard;

    // --- 2. 颜色精确插值 ---
    // 线段总长 (像素)
    float fullLen = input.halfLen_pixel * 2.0;

    // 计算当前片元在线段方向上的进度 t (范围 [0.0, 1.0])
    // 提示: localPos.x 在中心点为 0，起点处为 -halfLen，终点处为 +halfLen
    float t = (fullLen > 0.0001) ? ((input.localPos.x + input.halfLen_pixel) / fullLen) : 0.5;

    // clamp 的作用：对于圆角超出的部分 (超出起点或终点)，让其保持纯粹的起始/终止颜色
    t = clamp(t, 0.0, 1.0);

    // 线性插值混合起止颜色
    float4 baseColor = lerp(input.startColor, input.endColor, t);

    // 返回最终颜色 (带抗锯齿透明度)
    return float4(baseColor.rgb, alpha * baseColor.a);
}