// 文件: src/function/mask/mask_fragment_fs.wgsl (最终正确版)

struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@fragment
fn fs_main(@builtin(position) frag_coord: vec4<f32>) -> @location(0) vec4<f32> {
    // 安全检查
    if (uniforms.view.z <= 0.0 || uniforms.canvas.x <= 0.0) {
        discard;
    }

    // 1. 将像素坐标转换为世界坐标
    var ndc: vec2<f32>;
    ndc.x =  (frag_coord.x / uniforms.canvas.x) * 2.0 - 1.0;
    ndc.y = -(frag_coord.y / uniforms.canvas.y) * 2.0 + 1.0;
    let aspect = uniforms.view.w;
    let zoom = uniforms.view.z;
    let view_x = uniforms.view.x;
    let view_y = uniforms.view.y;
    var world_pos: vec2<f32>;
    world_pos.x = view_x + ndc.x * aspect / zoom;
    world_pos.y = view_y + ndc.y / zoom;

    let x = world_pos.x;
    let y = world_pos.y;

    // ✅ *** 核心修正：将变量名 F 改为 func_val ***
    // 2. 计算函数值
    let func_val = y - x;

    // 3. 计算函数 F 的梯度长度。
    let grad_len = sqrt(2.0);

    // 4. 使用 dpdx 计算一个像素在世界坐标中的宽度
    let pixel_width_in_world = length(dpdx(world_pos));

    // 5. 计算真正的距离
    // ✅ *** 核心修正：使用新的变量名 func_val ***
    let dist = abs(func_val) / grad_len;

    // 6. 我们的阈值就是半个像素的宽度
    let epsilon = pixel_width_in_world * 0.5;

    // 7. 如果距离大于半个像素，就抛弃
    if (dist > epsilon) {
        discard;
    }

    // 8. 如果在距离内，就画出平滑的抗锯齿线条
    let alpha = 1.0 - smoothstep(0.0, epsilon * 2.0, dist);

    return vec4<f32>(0.0, 0.0, 0.0, alpha); // 输出带透明度的黑色
}