struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
    params: vec4<f32>,
};

struct FunctionInfo {
    color: vec4<f32>,
};

struct Functions {
    data: array<FunctionInfo>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var<storage, read> functions: Functions;
@group(0) @binding(2) var validationMask: texture_2d<f32>;

/*__WGSL_FUNCTION_DEFINITIONS__*/

fn blend_colors(base: vec4<f32>, top: vec4<f32>) -> vec4<f32> {
    let out_alpha = top.a + base.a * (1.0 - top.a);
    if (out_alpha < 1.0e-6) { return vec4<f32>(0.0); }
    let out_rgb = (top.rgb * top.a + base.rgb * base.a * (1.0 - top.a)) / out_alpha;
    return vec4<f32>(out_rgb, out_alpha);
}

@fragment
fn fs_main(
    @location(0) world_pos: vec2<f32>,
    @builtin(position) frag_coord: vec4<f32>
) -> @location(0) vec4<f32> {
    let x = world_pos.x;
    let y = world_pos.y;
    var final_color = vec4<f32>(0.0);

    /*__WGSL_FUNCTION_EVALUATIONS__*/

    if (final_color.a < 0.01) {
        discard;
    }

    var point_found = false;
    let search_radius = 0.05;

    for (var dy = -search_radius; dy <= search_radius; dy = dy + 1) {
        for (var dx = -search_radius; dx <= search_radius; dx = dx + 1) {
            let offset = vec2<f32>(f32(dx), f32(dy));
            let sample_icoord = vec2<i32>(floor(frag_coord.xy + offset));
            let dims = textureDimensions(validationMask);
            if (sample_icoord.x < 0 || sample_icoord.x >= i32(dims.x) ||
                sample_icoord.y < 0 || sample_icoord.y >= i32(dims.y)) {
                continue;
            }
            let validation_sample = textureLoad(validationMask, sample_icoord, 0);
            if (validation_sample.r > 0.5) {
                point_found = true;
                break;
            }
        }
        if (point_found) { break; }
    }

    if (!point_found) {
        discard;
    }

    final_color = vec4<f32>(final_color.rgb * final_color.a, final_color.a);
    return final_color;
}