// 这个顶点着色器极其简单，它的唯一任务就是生成一个覆盖全屏的三角形
// 我们不需要任何顶点输入，而是直接在代码中定义顶点位置
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4<f32> {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    return vec4<f32>(positions[idx], 0.0, 1.0);
}