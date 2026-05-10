// triangles.slang
// 使用CPU预计算的归一化世界空间法线，多光源哑光光照。
// 顶点包含：位置（归一化世界坐标）、法线（已归一化）、颜色。

struct Light {
    float3 position;      // 归一化世界空间光源位置
    float  intensity;
    float3 color;
    float  _pad;          // std140对齐
};

struct SceneData {
    float4x4 vp;               // 视图投影矩阵（模型已合入顶点）
    float    ambientStrength;  // 环境光系数 (0~1)
    uint     lightCount;       // 有效光源数量
    float    _pad0;
    float    _pad1;
    Light    lights[16];       // 最多16个光源
};
[[vk::binding(0, 0)]] ConstantBuffer<SceneData> scene;

struct VSInput {
    [[vk::location(0)]] float3 pos    : POSITION;   // 归一化世界坐标
    [[vk::location(1)]] float3 normal : NORMAL;     // 归一化世界法线
    [[vk::location(2)]] float4 color  : COLOR0;
};

struct VSOutput {
    float4 clipPos      : SV_Position;
    float3 worldPos     : WORLDPOS;
    float3 worldNormal  : NORMAL0;
    float4 color        : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.clipPos     = mul(scene.vp, float4(input.pos, 1.0));
    output.worldPos    = input.pos;
    output.worldNormal = input.normal;        // 法线已归一化
    output.color       = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    // 插值后法线长度可能不为1，需要重新归一化
    float3 N = normalize(input.worldNormal);

    float3 diffuseSum = float3(0.0);
    for (uint i = 0; i < scene.lightCount; ++i) {
        Light L = scene.lights[i];
        float3 lightDir = normalize(L.position - input.worldPos);
        float NdotL = max(dot(N, lightDir), 0.0);
        diffuseSum += NdotL * L.intensity * L.color;
    }

    float3 ambient = scene.ambientStrength * input.color.rgb;
    float3 finalColor = (ambient + diffuseSum) * input.color.rgb;
    return float4(finalColor, input.color.a);
}