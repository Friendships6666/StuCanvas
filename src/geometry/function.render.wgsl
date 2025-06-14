// src/geometry/function.render.wgsl (回归正确版)

// ✅ 和计算着色器使用同一个Uniforms结构体
struct Uniforms {
    viewParams: vec4<f32>,
    canvasSize: vec2<f32>,
    lineThickness: f32,
    _padding: f32,
    color: vec4<f32>,
    mvpMatrix: mat4x4<f32>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@location(0) position: vec2<f32>) -> @builtin(position) vec4<f32> {
    // ✅ 使用 u.mvpMatrix 进行变换
    return u.mvpMatrix * vec4<f32>(position, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    // ✅ 使用 u.color 输出颜色
    return u.color;
}