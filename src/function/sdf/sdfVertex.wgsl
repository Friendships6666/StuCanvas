struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
    params: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) tex_coord: vec2<f32>, // 改名为 tex_coord
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(-1.0, 1.0),
        vec2<f32>(-1.0, 1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0)
    );
    let ndc_pos = positions[idx];

    var output: VertexOutput;
    output.clip_position = vec4<f32>(ndc_pos, 0.0, 1.0);
    // 生成纹理坐标 (0,0) to (1,1)
    output.tex_coord = ndc_pos * 0.5 + 0.5;
    // Y轴需要翻转以匹配纹理空间
    output.tex_coord.y = 1.0 - output.tex_coord.y;

    return output;
}