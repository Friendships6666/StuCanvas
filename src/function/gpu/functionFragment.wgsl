@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var<storage, read> functions: Functions;

// ✅ **核心修改**: 更新 Uniforms 结构体以匹配扩展后的缓冲区
struct Uniforms {
    view: vec4<f32>,     // .x: view.x, .y: view.y, .z: view.zoom, .w: aspect
    canvas: vec2<f32>,
    // .x: yMaxWorld for clipping. .yzw 是为了内存对齐而填充的，未使用
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

    let target_world_width = length(vec2(dpdx(x), dpdy(y)));

    /*__WGSL_FUNCTION_EVALUATIONS__*/

    return final_color;
}