@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var<storage, read> functions: Functions;

struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
    clip_params: vec4<f32>
};

struct Functions {
    data: array<vec4<f32>>
};

/*__WGSL_FUNCTION_DEFINITIONS__*/

// 混合函数 (这个是正确的，无需修改)
fn blend_colors(base: vec4<f32>, top_layer: vec4<f32>) -> vec4<f32> {
    let out_alpha = top_layer.a + base.a * (1.0 - top_layer.a);
    if (out_alpha < 1.0e-6) {
        return vec4<f32>(0.0);
    }
    let out_rgb = (top_layer.rgb * top_layer.a + base.rgb * base.a * (1.0 - top_layer.a)) / out_alpha;
    return vec4<f32>(out_rgb, out_alpha);
}

@fragment
fn fs_main(@location(0) world_pos: vec2<f32>) -> @location(0) vec4<f32> {
    let x = world_pos.x;
    let y = world_pos.y;
    var final_color = vec4<f32>(0.0, 0.0, 0.0, 0.0);

    /*__WGSL_FUNCTION_EVALUATIONS__*/

    // ✅ *** 核心修复 ***
    // 在返回颜色之前，将其转换为预乘 Alpha 格式。
    // 我们将之前错误的行替换为下面这行正确的 WGSL 语法。
    if (final_color.a > 0.0) {
        // 错误写法: final_color.rgb = final_color.rgb * final_color.a;
        // 正确写法: 创建一个新向量并赋回给 final_color
        final_color = vec4<f32>(final_color.rgb * final_color.a, final_color.a);
    }

    return final_color;
}