struct PointData
{
    float3 worldPos;   // Offset: 0,  Size: 12, Alignment: 16
    float _pad0;       // 🚀 Offset: 12, Size: 4  (手动补齐，使 color 能够落在 16 字节对齐点上) [1.1.1]
    float4 color;      // Offset: 16, Size: 16, Alignment: 16
};   // 🚀 PointData 整体大小正好为极其工整的 32 字节 (16 的倍数，在数组中排布最安全)

// 🚀 自包含点云数据结构
struct PointCloud
{
    float4x4 model;       // Offset: 0,  Size: 64, Alignment: 16 (包头)
    PointData points[];   // 🚀 Offset: 64, Size: N * 32, Alignment: 16 (载荷)
                          // 由于 64 已经是 16 的整倍数，此处天然完美对齐，无需任何填充！
};

struct PointSDFConstants
{
    float4x4 vp;         // Offset: 0,  Size: 64, Alignment: 16
    float2 imageSize;    // Offset: 64, Size: 8,  Alignment: 8
    float pointRadius;   // Offset: 72, Size: 4,  Alignment: 4
    float _pad0;         // 🚀 Offset: 76, Size: 4  (手动补齐，确保后续 64位物理指针按 8 字节边界对齐) [1.1.1]
    PointCloud* cloud;   // 🚀 Offset: 80, Size: 8,  Alignment: 8 (64位显存物理首地址) [1.1.1]
};   // 🚀 PointSDFConstants 整个常量块大小精确锁定在 88 字节
[[vk::push_constant]]
PointSDFConstants pc;

struct V2F
{
    float4 pos : SV_Position;
    float2 localCoord : TEXCOORD0;   // 顶点相对圆心的像素偏移
    float4 color : COLOR0;
};

static const float2 kCorners[ 3 ] = {
    float2 ( 0.0, 2.0 ),           // 上顶点
    float2 ( -1.7320508, -1.0 ),   // 左下顶点 (-sqrt(3), -1)
    float2 ( 1.7320508, -1.0 )     // 右下顶点 (sqrt(3), -1)
};

[ shader ( "vertex" ) ] V2F vertexMain ( uint vId : SV_VertexID, uint iId : SV_InstanceID )
{
    V2F outVert;
    uint corner = vId % 3;

    // 🚀 1. 通过统一指针，以 O(1) 的速度直接读取包头里的 Model 矩阵
    float4x4 model = pc.cloud->model;

    // 🚀 2. 自动越过前 64 字节，直接读取第 iId 个粒子数据 (内存布局完全在显卡硬件层自适应闭环)
    PointData pt = pc.cloud->points[ iId ];

    // 3. 物理变换计算
    float4 worldCenter = mul ( model, float4 ( pt.worldPos, 1.0 ) );
    float4 clipCenter = mul ( pc.vp, worldCenter );

    float2 pixelOffset = kCorners[ corner ] * pc.pointRadius * 2;
    float2 ndcOffset = ( pixelOffset * 2.0 ) / pc.imageSize;

    clipCenter.xy += ndcOffset * clipCenter.w;

    outVert.pos = clipCenter;
    outVert.localCoord = pixelOffset;
    outVert.color = pt.color;

    return outVert;
}

    [ shader ( "fragment" ) ] float4 fragmentMain ( V2F input )
    : SV_Target
{
    float dist = length ( input.localCoord );
    float radius = pc.pointRadius;
    float alpha = 1.0 - smoothstep ( radius - 1.5, radius, dist );

    if ( alpha <= 0.0 )
    {
        discard;
    }
    return float4 ( input.color.rgb, alpha * input.color.a );
}
