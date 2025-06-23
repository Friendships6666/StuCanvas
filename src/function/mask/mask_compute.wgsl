// 文件: src/function/mask/mask_compute.wgsl (决定性实验：可视化NDC)

struct Uniforms {
    // 这个实验甚至不需要 view，但我们保留它以匹配绑定组
    view: vec4<f32>,
    canvas: vec2<f32>,
};

@group(0) @binding(0) var mask_texture: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var<uniform> uniforms: Uniforms;

@compute
@workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    // 安全检查
    if (uniforms.canvas.x <= 0.0) { return; }
    if (id.x >= u32(uniforms.canvas.x) || id.y >= u32(uniforms.canvas.y)) {
        return;
    }

    // 1. ✅ 执行和之前失败的测试完全相同的NDC计算
    let ndc = (vec2<f32>(id.xy) / uniforms.canvas) * 2.0 - 1.0;

    // 2. ✅ 不使用if，而是将计算结果直接映射为颜色
    // NDC坐标范围是 -1.0 到 1.0。我们通过 * 0.5 + 0.5 将其映射到 0.0 到 1.0 的颜色范围。
    let color_r = ndc.x * 0.5 + 0.5;
    let color_g = ndc.y * 0.5 + 0.5;

    // 3. 将这个颜色写入纹理
    textureStore(mask_texture, vec2<i32>(id.xy), vec4<f32>(color_r, color_g, 0.5, 1.0));
}