struct Uniforms {
    viewParams: vec4<f32>, // x, y, zoom, aspect
    canvasSize: vec2<f32>, // width, height
};

struct FunctionData {
    color: vec4<f32>,
};

struct Functions {
    data: array<FunctionData>,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<storage, read> functions: Functions;

// ✅ Placeholder for all the 'fn eval_F_i(x,y) -> f32' function definitions
/*__WGSL_FUNCTION_DEFINITIONS__*/

@fragment
fn fs_main(@builtin(position) frag_coord: vec4<f32>) -> @location(0) vec4<f32> {
    // 1. Convert pixel coordinates to world coordinates
    let clip_x = frag_coord.x / u.canvasSize.x * 2.0 - 1.0;
    let clip_y = (1.0 - frag_coord.y / u.canvasSize.y) * 2.0 - 1.0;
    let x = u.viewParams.x + (clip_x / u.viewParams.z) * u.viewParams.w;
    let y = u.viewParams.y + (clip_y / u.viewParams.z);

    // 2. Define the target line width on screen (1.5 pixels)
    //    and calculate its corresponding size in world coordinates robustly.
    let target_world_width = length(vec2(dpdx(x), dpdy(y))) * 1.5;

    var final_color = vec4(0.0);

    // ✅ Placeholder for the evaluation logic that calls the functions above
    /*__WGSL_FUNCTION_EVALUATIONS__*/

    if (final_color.a <= 0.0) {
        discard;
    }

    return final_color;
}