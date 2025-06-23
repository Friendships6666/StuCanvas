// 文件: src/function/mask/mask_vertex_fs.wgsl (最终正确版)

// ✅ 这个着色器现在极其简单，它不输出任何自定义数据。
// 它的唯一职责就是告诉GPU光栅化一个覆盖全屏的三角形。
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4<f32> {
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    return vec4<f32>(positions[idx], 0.0, 1.0);
}