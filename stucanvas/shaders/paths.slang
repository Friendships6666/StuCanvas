// path_3d_strip.slang

struct Globals {
    float4x4 mvp;
    float2 viewportSize;
    float strokeWidth; // 像素宽度
};
[[vk::push_constant]] Globals pc;

struct ControlPoint {
    float3 pos;
    float  _pad;
    float4 color;
};

// 现在绑定的是整个点库
[[vk::binding(0, 0)]] StructuredBuffer<ControlPoint> allPoints;

struct V2F {
    float4 pos : SV_Position;
    float2 pixelPos : TEXCOORD0;
    nointerpolation uint instanceId : INST_ID;
};

static const float2 kRectCorners[4] = {
    float2(-1.0, -1.0), float2( 1.0, -1.0),
    float2( 1.0,  1.0), float2(-1.0,  1.0)
};
static const uint kTriIndices[6] = { 0, 1, 2, 0, 2, 3 };

// 3D 投影到像素空间
float2 projectToPixel(float3 pos, float4x4 mvp, float2 viewport) {
    float4 clip = mul(mvp, float4(pos, 1.0));
    float2 ndc = clip.xy / clip.w;
    return (ndc * 0.5 + 0.5) * viewport;
}

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID) {
    V2F output;
    uint cornerIdx = kTriIndices[vId % 6];

    // --- Strip 索引逻辑 ---
    // 第 iId 段曲线对应的起始点索引是 iId * 3
    uint base = iId * 3;
    float3 p0 = allPoints[base + 0].pos;
    float3 p1 = allPoints[base + 1].pos;
    float3 p2 = allPoints[base + 2].pos;
    float3 p3 = allPoints[base + 3].pos;

    // 投影到像素空间计算包围盒
    float2 px0 = projectToPixel(p0, pc.mvp, pc.viewportSize);
    float2 px1 = projectToPixel(p1, pc.mvp, pc.viewportSize);
    float2 px2 = projectToPixel(p2, pc.mvp, pc.viewportSize);
    float2 px3 = projectToPixel(p3, pc.mvp, pc.viewportSize);

    float2 minPx = min(min(px0, px1), min(px2, px3));
    float2 maxPx = max(max(px0, px1), max(px2, px3));

    float padding = pc.strokeWidth * 0.5 + 2.0;
    minPx -= padding;
    maxPx += padding;

    float2 cornerPx = lerp(minPx, maxPx, kRectCorners[cornerIdx] * 0.5 + 0.5);
    float2 ndc = (cornerPx / pc.viewportSize) * 2.0 - 1.0;

    // 深度处理：取 4 个控制点投影后的平均深度
    // 实际应用中也可取中心点深度
    float4 c0 = mul(pc.mvp, float4(p0, 1.0));
    float4 c3 = mul(pc.mvp, float4(p3, 1.0));
    float avgZ = (c0.z / c0.w + c3.z / c3.w) * 0.5;

    output.pos = float4(ndc, avgZ, 1.0);
    output.pixelPos = cornerPx;
    output.instanceId = iId;
    return output;
}

// ── 数学工具 ──
float2 cubicBezier(float2 p0, float2 p1, float2 p2, float2 p3, float t) {
    float u = 1.0 - t;
    float tt = t * t;
    float uu = u * u;
    return uu * u * p0 + 3.0 * uu * t * p1 + 3.0 * u * tt * p2 + tt * t * p3;
}

float4 interpolateColor(float4 c0, float4 c1, float4 c2, float4 c3, float t) {
    float u = 1.0 - t;
    float tt = t * t;
    float uu = u * u;
    return uu * u * c0 + 3.0 * uu * t * c1 + 3.0 * u * tt * c2 + tt * t * c3;
}

float sdSegment(float2 p, float2 a, float2 b, out float h) {
    float2 pa = p - a, ba = b - a;
    h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    uint base = input.instanceId * 3;

    // 获取 3D 控制点并投影
    float2 px0 = projectToPixel(allPoints[base+0].pos, pc.mvp, pc.viewportSize);
    float2 px1 = projectToPixel(allPoints[base+1].pos, pc.mvp, pc.viewportSize);
    float2 px2 = projectToPixel(allPoints[base+2].pos, pc.mvp, pc.viewportSize);
    float2 px3 = projectToPixel(allPoints[base+3].pos, pc.mvp, pc.viewportSize);

    const int STEPS = 12;
    float minDist = 1e8;
    float bestT = 0.0;

    float2 lastB = px0;
    for (int i = 1; i <= STEPS; ++i) {
        float t = float(i) / float(STEPS);
        float2 nextB = cubicBezier(px0, px1, px2, px3, t);

        float h;
        float d = sdSegment(input.pixelPos, lastB, nextB, h);
        if (d < minDist) {
            minDist = d;
            bestT = (float(i - 1) + h) / float(STEPS);
        }
        lastB = nextB;
    }

    float dist = minDist - (pc.strokeWidth * 0.5);
    float alpha = smoothstep(1.5, 0.0, dist);
    if (alpha <= 0.0) discard;

    // 颜色插值
    float4 c0 = allPoints[base+0].color;
    float4 c1 = allPoints[base+1].color;
    float4 c2 = allPoints[base+2].color;
    float4 c3 = allPoints[base+3].color;
    float4 color = interpolateColor(c0, c1, c2, c3, bestT);

    return float4(color.rgb, color.a * alpha);
}