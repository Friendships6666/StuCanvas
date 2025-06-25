/*src/function/SDF/functionVertex.wgsl*/
// 我们需要和片段着色器一样的 Uniforms 结构体
struct Uniforms {
    view: vec4<f32>,     // .x: view.x, .y: view.y, .z: view.zoom, .w: aspect
    canvas: vec2<f32>,
    clip_params: vec4<f32>
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// ✅ *** 核心修复 ***
// 定义一个输出结构体，这是顶点和片段着色器之间的“合同”
struct VertexOutput {
    // @builtin(position) 是必须的，它告诉 GPU 顶点最终在屏幕上的位置
    @builtin(position) clip_position: vec4<f32>,

    // @location(0) 是我们自定义的输出，必须和片段着色器的输入匹配
    @location(0) world_position: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
    // 定义一个覆盖全屏的矩形（由两个三角形组成）的六个顶点
    // 这些是在标准化设备坐标 (NDC) 中，从 -1 到 1
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0), // 左下
        vec2<f32>(1.0, -1.0),  // 右下
        vec2<f32>(-1.0, 1.0),  // 左上
        vec2<f32>(-1.0, 1.0),  // 左上
        vec2<f32>(1.0, -1.0),  // 右下
        vec2<f32>(1.0, 1.0)   // 右上
    );

    let ndc_pos = positions[idx];

    var output: VertexOutput;

    // 1. 设置内置的 clip_position，用于光栅化。z=0, w=1 适用于 2D
    output.clip_position = vec4<f32>(ndc_pos, 0.0, 1.0);

    // 2. 计算并设置要传递给片段着色器的 world_position
    // 这是核心的逆变换：从 NDC 坐标反推回世界坐标
    let aspect = uniforms.view.w;
    let zoom = uniforms.view.z;
    let view_x = uniforms.view.x;
    let view_y = uniforms.view.y;

    // 水平方向：从 NDC (-1, 1) 映射到世界坐标
    output.world_position.x = view_x + ndc_pos.x * aspect / zoom;
    // 垂直方向：从 NDC (-1, 1) 映射到世界坐标
    output.world_position.y = view_y + ndc_pos.y / zoom;

    return output;
}