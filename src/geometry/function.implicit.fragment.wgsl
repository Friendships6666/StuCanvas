struct Uniforms {
    viewParams: vec4<f32>, // 存储相机信息: x, y, zoom, aspect
    canvasSize: vec2<f32>, // 存储画布尺寸: width, height
    _padding1: f32,
    _padding2: f32,
    color: vec4<f32>,      // 存储线条颜色
}

@group(0) @binding(0) var<uniform> u: Uniforms;

// ✅ 更改函数签名，接受 x 和 y，并将其返回视为 F(x,y)
fn graph_equation_F(x: f32, y: f32) -> f32 {
    /*__WGSL_GRAPH_FUNCTION_BODY__*/return y - (x*x);/*__WGSL_GRAPH_FUNCTION_BODY__*/
    // 注入的代码现在将是 F(x,y) = 0 形式，例如 for y = x*x 返回 y - x*x
    // for x*x + y*y = 9 返回 x*x + y*y - 9
}

@fragment
fn fs_main(@builtin(position) frag_coord: vec4<f32>) -> @location(0) vec4<f32> {
    // 1. 将像素坐标转换为世界坐标
    let clip_x = frag_coord.x / u.canvasSize.x * 2.0 - 1.0;
    let clip_y = (1.0 - frag_coord.y / u.canvasSize.y) * 2.0 - 1.0;
    let world_x = u.viewParams.x + (clip_x / u.viewParams.z) * u.viewParams.w;
    let world_y = u.viewParams.y + (clip_y / u.viewParams.z);

    // 2. 使用与像素尺寸匹配的动态步长 'h' 进行数值求导
    // fwidth(world_x) 能精确返回一个像素在世界X轴上的宽度，这是最鲁棒的步长选择
    let h = fwidth(world_x);

    // 3. 使用这个鲁棒的 'h' 来计算导数和梯度
    // ✅ 这里求导需要考虑对 x 和 y 都进行求导，以获得真正的梯度。
    // For F(x,y)=0, the gradient vector is (dF/dx, dF/dy).
    let df_dx = (graph_equation_F(world_x + h, world_y) - graph_equation_F(world_x - h, world_y)) / (2.0 * h);
    let df_dy = (graph_equation_F(world_x, world_y + h) - graph_equation_F(world_x, world_y - h)) / (2.0 * h);
    let gradient_length = length(vec2(df_dx, df_dy)); // 这是真正法线的长度

    // 4. 计算归一化的、近似垂直的距离 (单位：世界坐标)
    // ✅ dist 现在直接就是 F(world_x, world_y) 的值
    let dist = graph_equation_F(world_x, world_y);
    let normalized_dist = abs(dist) / max(gradient_length, 1e-16); // max 避免除以零

    // 5. 定义线条的目标宽度，单位也是世界坐标
    //    目标是在屏幕上恒定为 1.5 像素宽
    let target_world_width = h * 1.5;

    // 6. 使用正确的归一化距离和世界宽度进行平滑插值
    //    当距离从 0 变到目标宽度时，alpha 从 1 平滑过渡到 0
    let alpha = smoothstep(target_world_width, 0.0, normalized_dist);

    if (alpha <= 0.0) {
        discard;
    }

    return vec4<f32>(u.color.rgb, u.color.a * alpha);
}