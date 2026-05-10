// point.slang
// 使用一个三角形实例绘制每个点，通过 SDF 实现抗锯齿圆。

// 全局参数，通过 push constants 传入（MVP、分辨率、半径等）
struct Globals {
    float4x4 mvp;               // 列主序 MVP 矩阵
    float2 viewportSize;        // 屏幕像素尺寸
    float pointRadius;          // 圆的屏幕像素半径
};
[[vk::push_constant]] Globals pc;

// 每个点的数据，存储在一个 StructuredBuffer 中
struct PointData {
    float3 worldPos;            // 归一化世界坐标
    float4 color;
};
[[vk::binding(0, 0)]] StructuredBuffer<PointData> points;

// 片元着色器输入
struct V2F {
    float4 pos : SV_Position;
    float2 localCoord : TEXCOORD0; // 顶点相对圆心的像素偏移
    float4 color : COLOR0;
};

// 固定三角形顶点坐标（以圆心为原点，半径为 1 的覆盖）
static const float2 kCorners[3] = {
    float2(0.0, 1.732),   // 上顶点
    float2(-1.5, -0.866), // 左下
    float2(1.5, -0.866)   // 右下
};

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    V2F outVert;
    uint corner = vId % 3;                // 三角形三个顶点
    PointData pt = points[iId];           // 当前点的数据

    float4 worldCenter = float4(pt.worldPos, 1.0);
    float4 clipCenter = mul(pc.mvp, worldCenter);

    // 三角形顶点相对于圆心的像素偏移，并做透视 w 补偿
    float2 pixelOffset = kCorners[corner] * pc.pointRadius;
    float2 ndcOffset = pixelOffset / pc.viewportSize;   // 像素 -> NDC
    clipCenter.xy += ndcOffset * clipCenter.w;

    outVert.pos = clipCenter;
    outVert.localCoord = pixelOffset;   // 用于片元中计算距离
    outVert.color = pt.color;
    return outVert;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target
{
    float dist = length(input.localCoord);
    float radius = pc.pointRadius;
    // 使用 smoothstep 产生约 1.5 像素的抗锯齿过渡带
    float alpha = smoothstep(radius, radius - 1.5, dist);
    return float4(input.color.rgb, alpha * input.color.a);
}