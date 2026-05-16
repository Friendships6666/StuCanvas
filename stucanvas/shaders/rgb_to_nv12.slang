// stucanvas/shaders/rgb_to_nv12.slang

// BT.709 转换系数 (有限范围 Limited Range, 视频标准)
static const float3 K_Y = float3(0.2126, 0.7152, 0.0722);
static const float3 K_U = float3(-0.1146, -0.3854, 0.5);
static const float3 K_V = float3(0.5, -0.4542, -0.0458);

// 输入：渲染好的 8K RGB 图像
[[vk::binding(0, 0)]] Texture2D<float4> inputRGB;

// 输出：NV12 的两个平面
// 在 Vulkan 中，多平面图像的每个 Plane 会被绑定为独立的 ImageView
[[vk::binding(1, 0)]] RWTexture2D<float> outputY;   // Plane 0: 8K 单通道
[[vk::binding(2, 0)]] RWTexture2D<float2> outputUV; // Plane 1: 4K 双通道 (RG8 -> UV)

[shader("compute")]
[numthreads(16, 16, 1)] // 每个工作组处理 32x32 个 RGB 像素
void computeMain(uint3 threadId : SV_DispatchThreadID) {
    // 每个线程负责输出 4 个 Y 像素和 1 个 UV 像素对 (4:2:0 下采样)
    uint2 baseCoord = threadId.xy * 2;

    // 1. 读取 2x2 范围内的 4 个 RGB 像素
    float3 rgb00 = inputRGB.Load(int3(baseCoord + uint2(0, 0), 0)).rgb;
    float3 rgb10 = inputRGB.Load(int3(baseCoord + uint2(1, 0), 0)).rgb;
    float3 rgb01 = inputRGB.Load(int3(baseCoord + uint2(0, 1), 0)).rgb;
    float3 rgb11 = inputRGB.Load(int3(baseCoord + uint2(1, 1), 0)).rgb;

    // 2. 计算 4 个亮度值 (Y)
    // 视频标准通常使用 Limited Range (16-235), 这里按线性 [0,1] 计算
    // 驱动的编码器会自动处理到 8-bit 的映射
    float y00 = dot(rgb00, K_Y);
    float y10 = dot(rgb10, K_Y);
    float y01 = dot(rgb01, K_Y);
    float y11 = dot(rgb11, K_Y);

    // 写入 Y 平面 (8K 分辨率)
    outputY[baseCoord + uint2(0, 0)] = y00;
    outputY[baseCoord + uint2(1, 0)] = y10;
    outputY[baseCoord + uint2(0, 1)] = y01;
    outputY[baseCoord + uint2(1, 1)] = y11;

    // 3. 计算色度值 (UV) - 4:2:0 下采样
    // 取 2x2 像素的平均值来计算 U 和 V
    float3 avgRGB = (rgb00 + rgb10 + rgb01 + rgb11) * 0.25;

    float u = dot(avgRGB, K_U) + 0.5;
    float v = dot(avgRGB, K_V) + 0.5;

    // 写入 UV 平面 (4K 分辨率, 每个像素存一对 UV)
    outputUV[threadId.xy] = float2(u, v);
}