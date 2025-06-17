/*src/function/gpu/functionFragment.wgsl*/
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

@fragment
fn fs_main(@location(0) world_pos: vec2<f32>) -> @location(0) vec4<f32> {
    let x = world_pos.x;
    let y = world_pos.y;
    var final_color = vec4<f32>(0.0);

    // 核心修改：移除所有具体逻辑，只留下一个占位符
    /*__WGSL_FUNCTION_EVALUATIONS__*/

    return final_color;
}