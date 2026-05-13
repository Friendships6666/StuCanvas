// 使用一个三角形实例绘制每个点，通过 SDF 实现抗锯齿圆。

// ==========================================
// 1. 全局参数 (Push Constants)
// ==========================================
struct Globals {
    float4x4 mvp;               // 列主序 MVP 矩阵 (64 bytes)
    float2 viewportSize;        // 屏幕像素尺寸 (8 bytes)
    float pointRadius;          // 圆的屏幕像素半径 (4 bytes)
};

[[vk::push_constant]]
Globals pc;

// ==========================================
// 2. 顶点数据缓存 (StructuredBuffer)
// ==========================================
struct PointData {
    float3 worldPos;            // 归一化世界坐标 (12 bytes)
    float  _pad0;               // 显式 4 字节填充，保证 color 严格在 16 字节偏移对齐
    float4 color;               // 颜色 (r, g, b, a) (16 bytes)
};

[[vk::binding(0, 0)]]
StructuredBuffer<PointData> points;

// ==========================================
// 3. 顶点着色器输出 / 片元着色器输入
// ==========================================
struct V2F {
    float4 pos : SV_Position;
    float2 localCoord : TEXCOORD0; // 顶点相对圆心的像素偏移
    float4 color : COLOR0;
};

// ==========================================
// 4. 代理三角形的本地顶点坐标
// ==========================================
// 此处使用一个包裹了半径为 1 的内切圆的正三角形。
// 当圆半径为 1 时，完美外接的正三角形的外接圆半径为 2。
static const float2 kCorners[3] = {
    float2(0.0, 2.0),           // 上顶点
    float2(-1.7320508, -1.0),   // 左下顶点 (-sqrt(3), -1)
    float2(1.7320508, -1.0)     // 右下顶点 (sqrt(3), -1)
};

// ==========================================
// 5. 顶点着色器阶段
// ==========================================
[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    V2F outVert;

    // 当前实例(点)对应的数据，以及当前顶点所属的三角形角标
    uint corner = vId % 3;
    PointData pt = points[iId];

    // 计算点的中心在裁剪空间的坐标
    float4 worldCenter = float4(pt.worldPos, 1.0);
    float4 clipCenter = mul(pc.mvp, worldCenter);

    // 将顶点按半径展开，获得当前顶点的相对像素偏移量
    float2 pixelOffset = kCorners[corner] * pc.pointRadius;

    // 像素偏移转 NDC 偏移：NDC 的跨度是 2 (从 -1 到 1)，所以要乘 2.0
    float2 ndcOffset = (pixelOffset * 2.0) / pc.viewportSize;

    // 把偏移量加到 clip space xy 上
    // 乘以 clipCenter.w 是为了抵消光栅化阶段除以 w 的透视除法，保证其屏幕尺寸始终恒定
    clipCenter.xy += ndcOffset * clipCenter.w;

    outVert.pos = clipCenter;
    outVert.localCoord = pixelOffset; // 将未受透视形变的本地像素偏移传给片元着色器
    outVert.color = pt.color;

    return outVert;
}

// ==========================================
// 6. 片元着色器阶段
// ==========================================
[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target
{
    // 计算当前片元距离圆心的像素距离
    float dist = length(input.localCoord);
    float radius = pc.pointRadius;

    // 使用 smoothstep 产生约 1.5 像素的抗锯齿过渡带
    // dist 越小 (靠近圆心)，alpha 越接近 1.0；dist 大于 radius 时，alpha 为 0
    float alpha = 1.0 - smoothstep(radius - 1.5, radius, dist);

    // 如果已经完全超出半径，直接 discard 以优化性能 (可选)
    if (alpha <= 0.0) {
        discard;
    }

    // 将计算出的 SDF 覆盖度与原始顶点 Alpha 相乘
    return float4(input.color.rgb, alpha * input.color.a);
}