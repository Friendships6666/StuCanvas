// triangles.slang - 修复 Validation 警告，改为 SSBO 访问

struct Globals {
    float4x4 mvp;
    float3   camPos;
    float    _pad0;
    float2   viewportSize;
    float    pointRadius;
    float    _pad1;
};
[[vk::push_constant]] Globals pc;

// 定义与 PointDataGPU 一致的结构
struct Vertex {
    float3 pos;
    float  _pad;
    float4 color;
};

// 使用绑定 0 的存储缓冲，而不是 Location 输入
[[vk::binding(0, 0)]] StructuredBuffer<Vertex> vertices;

struct V2F {
    float4 clipPos  : SV_Position;
    float3 worldPos : TEXCOORD0;
    float4 color    : COLOR;
};

[shader("vertex")]
V2F vertexMain(uint vId : SV_VertexID) { // 使用 vId 自动索引
    V2F output;

    // 从 SSBO 中读取顶点
    Vertex v = vertices[vId];

    output.clipPos = mul(pc.mvp, float4(v.pos, 1.0));
    output.worldPos = v.pos;
    output.color = v.color;

    return output;
}

[shader("fragment")]
float4 fragmentMain(V2F input) : SV_Target {
    // --- 实时面法线计算 (ddx/ddy) ---
    float3 dX = ddx(input.worldPos);
    float3 dY = ddy(input.worldPos);
    float3 faceNormal = normalize(cross(dX, dY));

    // --- 相机视线与双面光照 ---
    float3 viewDir = normalize(pc.camPos - input.worldPos);
    if (dot(faceNormal, viewDir) < 0.0) {
        faceNormal = -faceNormal;
    }

    // --- 硬编码光源 ---
    const float3 lightPos = float3(5.0, 5.0, 10.0);
    const float  lightIntensity = 0.9;
    const float  ambientStrength = 0.25;

    float3 lightDir = normalize(lightPos - input.worldPos);
    float diff = max(dot(faceNormal, lightDir), 0.0);

    float3 finalRGB = (ambientStrength + diff * lightIntensity) * input.color.rgb;
    return float4(finalRGB, input.color.a);
}