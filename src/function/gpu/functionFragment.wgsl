@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var<storage, read> functions: Functions;

struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
    clip_params: vec4<f32>,
    // ✅ 新增: 调试模式开关 (1.0 for true, 0.0 for false)
    debug_mode: f32,
};

struct Functions {
    data: array<vec4<f32>>
};

// ✅ 新增: 定义一个包含颜色和调试值的输出结构体
struct FragmentOutput {
    @location(0) color: vec4<f32>,
    @location(1) debug_value: f32,
};

/*__WGSL_FUNCTION_DEFINITIONS__*/

fn blend_colors(base: vec4<f32>, top_layer: vec4<f32>) -> vec4<f32> {
    let out_alpha = top_layer.a + base.a * (1.0 - top_layer.a);
    if (out_alpha < 1.0e-6) {
        return vec4<f32>(0.0);
    }
    let out_rgb = (top_layer.rgb * top_layer.a + base.rgb * base.a * (1.0 - top_layer.a)) / out_alpha;
    return vec4<f32>(out_rgb, out_alpha);
}

@fragment
// ✅ 修改: fs_main 的返回值现在是 FragmentOutput 结构体
fn fs_main(@location(0) world_pos: vec2<f32>) -> FragmentOutput {
    let x = world_pos.x;
    let y = world_pos.y;
    var final_color = vec4<f32>(0.0, 0.0, 0.0, 0.0);

    // ✅ 新增: 初始化调试输出值，-1.0 作为“无有效值”的默认标记
    var out_debug_value = -1.0;

    /*__WGSL_FUNCTION_EVALUATIONS__*/

    // ✅ 在返回颜色之前，将其转换为预乘 Alpha 格式。
    if (final_color.a > 0.0) {
        final_color = vec4<f32>(final_color.rgb * final_color.a, final_color.a);
    }

    var output: FragmentOutput;
    output.color = final_color;
    output.debug_value = out_debug_value;

    return output;
}