// ==========================================
// 1. 数据结构 (自包含几何缓冲区设计)
// ==========================================
struct LineData
{
    float3 p0;      // 起点 3D 局部坐标
    float3 p1;      // 终点 3D 局部坐标
    float4 color;   // 线段颜色
};

struct LineGroup
{
    float4x4 model;     // 包头：当前线段组整体的 Model 变换矩阵 (Offset 0, Size 64)
    LineData lines[];   // 载荷：线段局部数据数组 (Offset 64)
};

struct LineSDFConstants
{
    float4x4 vp;        // 观察-投影矩阵 (Offset 0, Size 64)
    float2 imageSize;   // 物理画布的宽高像素尺寸 (Offset 64, Size 8)
    float lineWidth;    // 线段像素宽度 (Offset 72, Size 4)
    LineGroup* group;   // 🚀 64位 GPU 物理显存指针，指向自包含缓冲区首地址 (Offset 80, Size 8)
};

[[vk::push_constant]]
LineSDFConstants pc;

// ==========================================
// 2. 顶点着色器输出 / 片元着色器输入
// ==========================================
struct V2F
{
    float4 pos : SV_Position;
    float2 localPos : TEXCOORD0;                 // 当前片元在线段局部坐标系下的位置
    nointerpolation float lengthL : TEXCOORD1;   // 当前线段在屏幕上的总像素长度 (常量无需插值)
    float4 color : COLOR0;
};

// ==========================================
// 3. 顶点着色器阶段
// ==========================================
[ shader ( "vertex" ) ] V2F vertexMain ( uint vId : SV_VertexID, uint iId : SV_InstanceID )
{
    V2F outVert;

    uint corner = vId % 6;

    // 🚀 1. 从自包含缓冲区的包头中，以 O(1) 的速度直接读取 Model 矩阵
    float4x4 model = pc.group->model;

    // 🚀 2. 自动越过前 64 字节，直接读取当前实例对应的线段数据
    LineData lineData = pc.group->lines[ iId ];

    // 🚀 3. 第一步：使用 Model 矩阵将起终点的局部坐标变换到世界空间
    float4 world0 = mul ( model, float4 ( lineData.p0, 1.0 ) );
    float4 world1 = mul ( model, float4 ( lineData.p1, 1.0 ) );

    // 🚀 4. 第二步：使用影子相机的归一化 vp 矩阵，变换到裁剪空间 (Clip Space)
    float4 clip0 = mul ( pc.vp, world0 );
    float4 clip1 = mul ( pc.vp, world1 );

    // 简单防退化保护：防止点在摄像机后方导致透视除法翻转
    if ( clip0.w < 0.0001 )
        clip0.w = 0.0001;
    if ( clip1.w < 0.0001 )
        clip1.w = 0.0001;

    // 透视除法转为 NDC 坐标 [-1, 1]
    float2 ndc0 = clip0.xy / clip0.w;
    float2 ndc1 = clip1.xy / clip1.w;

    // 转换为绝对屏幕像素坐标
    float2 scr0 = ndc0 * pc.imageSize * 0.5;
    float2 scr1 = ndc1 * pc.imageSize * 0.5;

    // 计算屏幕上线段的方向与长度
    float2 segmentVec = scr1 - scr0;
    float L = length ( segmentVec );
    float2 dir = ( L > 0.0001 ) ? ( segmentVec / L ) : float2 ( 1.0, 0.0 );   // 切线向量
    float2 norm = float2 ( -dir.y, dir.x );                                   // 法线向量

    // 计算外扩范围 (半径 + 1.5像素的 SDF 抗锯齿羽化带)
    float radius = pc.lineWidth * 0.5;
    float ext = radius + 1.5;

    // 构建一个包裹该线段的 2D 矩形 (6个顶点组成 2 个三角形)
    // 局部坐标系定义：原点在 scr0，X轴沿线段方向，Y轴沿法线方向
    // 0: 左上, 1: 左下, 2: 右上  |  3: 右上, 4: 左下, 5: 右下
    float2 localCoords[ 6 ] = { float2 ( -ext, ext ),    float2 ( -ext, -ext ), float2 ( L + ext, ext ),
                                float2 ( L + ext, ext ), float2 ( -ext, -ext ), float2 ( L + ext, -ext ) };

    float2 lPos = localCoords[ corner ];

    // 判断当前生成的外扩顶点归属于起点还是终点，以使用正确的 Z 和 W 进行插值
    bool isEnd = ( corner == 2 || corner == 3 || corner == 5 );
    float4 baseClip = isEnd ? clip1 : clip0;

    // 在屏幕像素空间计算该顶点的绝对物理坐标
    float2 targetScr = scr0 + dir * lPos.x + norm * lPos.y;

    // 将屏幕坐标反推回 NDC，并乘以 baseClip.w 抵消后续的光栅化透视除法
    float2 targetNdc = targetScr / ( pc.imageSize * 0.5 );
    outVert.pos = float4 ( targetNdc * baseClip.w, baseClip.z, baseClip.w );

    outVert.localPos = lPos;
    outVert.lengthL = L;
    outVert.color = lineData.color;

    return outVert;
}

    // ==========================================
    // 4. 片元着色器阶段
    // ==========================================
    [ shader ( "fragment" ) ] float4 fragmentMain ( V2F input )
    : SV_Target
{
    // 1. 将 X 轴坐标限制在线段本体长度 [0, L] 的区间内
    // 这样在端点处，closestPoint 就会停留在 0 或 L，从而自动形成“完美的圆角帽 (Round Caps)”
    float x_clamped = clamp ( input.localPos.x, 0.0, input.lengthL );
    float2 closestPoint = float2 ( x_clamped, 0.0 );

    // 2. 纯粹的 2D SDF：计算当前像素到线段最近点的几何距离
    float dist = length ( input.localPos - closestPoint );
    float radius = pc.lineWidth * 0.5;

    // 3. 计算 1.5 像素宽度的完美抗锯齿渐变
    float alpha = 1.0 - smoothstep ( radius - 1.5, radius, dist );

    // 4. Early-Z / Early-Discard 硬件提速：切掉完全透明的边界多余部分
    if ( alpha <= 0.0 )
    {
        discard;
    }

    return float4 ( input.color.rgb, alpha * input.color.a );
}
