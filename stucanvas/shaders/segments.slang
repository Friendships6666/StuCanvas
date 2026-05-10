struct Globals {
    float4x4 mvp;
    float2 viewportSize;
    float lineWidth;
};
[[vk::push_constant]] Globals pc;

struct Segment {
    float3 startPos;
    float3 endPos;
    float4 color;
};
[[vk::binding(0, 0)]] StructuredBuffer<Segment> segments;

struct V2F {
    float4 pos : SV_Position;
    float3 localParams : TEXCOORD0;   // (u_ratio, v_pixel, halfLen_pixel)
    float4 color : COLOR0;
};

// 矩形的 6 个顶点（两个三角形）
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
    float2 s0 = p0.xy / p0.w;
    float2 s1 = p1.xy / p1.w;

    float2 dir = s1 - s0;
    float len = length(dir);
    float2 unitDir = dir / len;
    float2 unitNormal = float2(-unitDir.y, unitDir.x);

    float halfW_ndc = pc.lineWidth * 0.5 / pc.viewportSize.x;

    // 矩形半长和半宽（NDC）
    float halfLen_ndc = len * 0.5 + halfW_ndc * 2.0;   // 扩展以覆盖端点
    float halfV_ndc  = halfW_ndc * 2.0;                // 垂直方向包含抗锯齿

    float2 mid = (s0 + s1) * 0.5;

    float2 localCorner = kRectCorners[corner];
    float u_ndc = localCorner.x * halfLen_ndc;
    float v_ndc = localCorner.y * halfV_ndc;
    float2 cornerNDC = mid + unitDir * u_ndc + unitNormal * v_ndc;

    float depth = (p0.z / p0.w + p1.z / p1.w) * 0.5;
    float w = (p0.w + p1.w) * 0.5;
    outVert.pos = float4(cornerNDC * w, depth * w, w);

    // 传递局部参数给片元
    float pixelScale = pc.viewportSize.x * 0.5;
    outVert.localParams = float3(localCorner.x,                    // u_ratio ∈ [-1,1]
                                 localCorner.y * halfV_ndc * pixelScale, // v_pixel
                                 halfLen_ndc * pixelScale);         // halfLen_pixel
    outVert.color = seg.color;
    return outVert;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    float u_ratio = input.localParams.x;        // [-1,1] 表示在线段上的相对位置
    float v_pixel = input.localParams.y;        // 垂直像素距离
    float halfLen_pixel = input.localParams.z; // 线段半长（像素）
    float halfW = pc.lineWidth * 0.5;

    // 计算点到线段的像素距离
    float du = abs(u_ratio) * halfLen_pixel - halfLen_pixel; // 超过端点的水平距离
    float dist;
    if (du <= 0.0) {
        dist = abs(v_pixel);                   // 线段内部，距离 = 垂直距离
    } else {
        dist = length(float2(du, v_pixel));    // 到端点的距离
    }
    dist -= halfW;

    float aa = 1.5; // 抗锯齿像素宽度
    float alpha = smoothstep(aa, -aa, dist);
    return float4(input.color.rgb, alpha * input.color.a);
}