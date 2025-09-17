// src/grid.wgsl

// 与主着色器共享的 Uniform 结构
struct Uniforms {
  screen_dimensions: vec2<f32>,
  zoom: f32,
  offset: vec2<f32>,
};

// 网格线专用的 Uniform，用于控制颜色
struct GridUniforms {
    color: vec4<f32>,
}

@group(0) @binding(0) var<uniform> r_params: Uniforms;
@group(0) @binding(1) var<uniform> grid_params: GridUniforms;

// 辅助函数：将世界坐标转换为裁剪空间坐标
fn world_to_clip(world_pos: vec2<f32>, uniforms: Uniforms) -> vec4<f32> {
    let screen_dims = uniforms.screen_dimensions;
    var temp_pos = (world_pos - uniforms.offset) * uniforms.zoom;
    temp_pos.x = temp_pos.x / (screen_dims.x / screen_dims.y);
    return vec4<f32>(temp_pos * 2.0, 0.0, 1.0);
}

@vertex
fn vs_main(@location(0) world_position: vec2<f32>) -> @builtin(position) vec4<f32> {
    return world_to_clip(world_position, r_params);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return grid_params.color;
}