// path_cubic.slang
// 三次贝塞尔路径渲染：四边形包围盒 + 迭代 SDF + 逐控制点颜色

struct Globals {
    float4x4 mvp;               // 视图投影矩阵
    float2 viewportSize;         // 屏幕物理像素尺寸
    float strokeWidth;          // 描边像素宽度
};
[[vk::push_constant]] Globals pc;

// 控制点 – 归一化世界坐标与颜色
struct ControlPoint {
    float2 pos;                 // 世界坐标
    float4 color;               // RGBA
};

// 完整的曲线段（4 个控制点）
struct CubicCurve {
    ControlPoint p0;
    ControlPoint p1;
    ControlPoint p2;
    ControlPoint p3;
};

[[vk::binding(0, 0)]] StructuredBuffer<CubicCurve> curves;

// ── 顶点输出 ──────────────────────────────
struct VSOutput {
    float4 pos : SV_Position;
    float2 worldPos : WORLDPOS;             // 四边形角点的世界坐标
    nointerpolation uint curveIndex : CURVE_INDEX;
};

// ── 三次贝塞尔求值 ─────────────────────────
float2 cubicBezier(float2 p0, float2 p1, float2 p2, float2 p3, float t) {
    float u = 1.0 - t;
    return u*u*u * p0 + 3.0*u*u*t * p1 + 3.0*u*t*t * p2 + t*t*t * p3;
}

// ── 点到曲线距离（迭代采样）───────────────
float distanceToCubic(float2 p, float2 p0, float2 p1, float2 p2, float2 p3,
                      out float bestT) {
    const int STEPS = 24;                   // 采样步数，可根据质量调整
    float d = 1e10;
    bestT = 0.0;
    for (int i = 0; i <= STEPS; ++i) {
        float t = float(i) / float(STEPS);
        float2 b = cubicBezier(p0, p1, p2, p3, t);
        float dist = length(p - b);
        if (dist < d) {
            d = dist;
            bestT = t;
        }
    }
    return d;
}

// ── 颜色插值（de Casteljau）───────────────
float4 interpolateColor(float4 c0, float4 c1, float4 c2, float4 c3, float t) {
    float4 a = lerp(c0, c1, t);
    float4 b = lerp(c1, c2, t);
    float4 c = lerp(c2, c3, t);
    float4 d = lerp(a, b, t);
    float4 e = lerp(b, c, t);
    return lerp(d, e, t);
}

// ══════════════════════════════════════════
//  顶点着色器
// ══════════════════════════════════════════
[shader("vertex")]
VSOutput vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    CubicCurve curve = curves[iId];

    // 控制点位置
    float2 p0 = curve.p0.pos;
    float2 p1 = curve.p1.pos;
    float2 p2 = curve.p2.pos;
    float2 p3 = curve.p3.pos;

    // 1. 轴对齐包围盒
    float2 minPt = min(min(p0, p1), min(p2, p3));
    float2 maxPt = max(max(p0, p1), max(p2, p3));

    // 2. 扩展描边宽度（归一化世界坐标单位）
    //    一个像素对应世界坐标 2.0 / viewportSize.x （因为世界坐标范围 [-1,1] 对应整个视口）
    float halfStrokeWorld = pc.strokeWidth / pc.viewportSize.x;  // 半边
    minPt -= halfStrokeWorld;
    maxPt += halfStrokeWorld;

    // 3. 四边形四个角点（按三角形列表顺序 0-1-2, 0-2-3）
    float2 corners[4] = {
        float2(minPt.x, minPt.y),
        float2(maxPt.x, minPt.y),
        float2(maxPt.x, maxPt.y),
        float2(minPt.x, maxPt.y)
    };
    uint vertexIdx = vId % 4;
    float2 corner = corners[vertexIdx];

    // 4. 输出
    VSOutput output;
    output.pos = mul(pc.mvp, float4(corner, 0.0, 1.0));
    output.worldPos = corner;
    output.curveIndex = iId;
    return output;
}

// ══════════════════════════════════════════
//  片段着色器
// ══════════════════════════════════════════
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    uint idx = input.curveIndex;
    CubicCurve curve = curves[idx];
    float2 p0 = curve.p0.pos;
    float2 p1 = curve.p1.pos;
    float2 p2 = curve.p2.pos;
    float2 p3 = curve.p3.pos;

    // 1. 计算距离
    float bestT;
    float dist = distanceToCubic(input.worldPos, p0, p1, p2, p3, bestT);

    // 2. 描边半宽（世界单位）
    float halfStroke = pc.strokeWidth / pc.viewportSize.x;

    // 3. SDF 抗锯齿
    float signedDist = abs(dist) - halfStroke;
    float aaWidth = 1.5 / pc.viewportSize.x;   // 1.5 像素过渡带
    float alpha = smoothstep(aaWidth, -aaWidth, signedDist);

    // 4. 插值颜色
    float4 color = interpolateColor(curve.p0.color, curve.p1.color,
                                    curve.p2.color, curve.p3.color, bestT);
    color.a *= alpha;
    return color;
}