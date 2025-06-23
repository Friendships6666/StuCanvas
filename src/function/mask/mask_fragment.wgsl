// 绑定 0: 我们在计算通道中生成好的蒙版纹理
@group(0) @binding(0) var mask_texture: texture_2d<f32>;
// 绑定 1: 一个采样器，用于读取纹理
@group(0) @binding(1) var my_sampler: sampler;

@fragment
fn fs_main(@builtin(position) frag_coord: vec4<f32>) -> @location(0) vec4<f32> {
    // 将像素坐标转换为纹理坐标 (0.0 to 1.0)
    let tex_coord = frag_coord.xy / vec2<f32>(textureDimensions(mask_texture));

    // 从蒙版纹理中采样颜色
    let mask_color = textureSample(mask_texture, my_sampler, tex_coord);

    // 直接返回蒙版的颜色。如果计算着色器没有在该位置写入，
    // 纹理的默认颜色是透明黑 (0,0,0,0)，所以屏幕上不会显示任何东西。
    return mask_color;
}