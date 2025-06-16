/*src/function/functionVertex.wgsl*/
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
    // ✅ 最终修正: 使用 switch 语句替代数组，以获得最大兼容性
    var position: vec2<f32>;

    switch (vertexIndex) {
        // 第一个三角形
        case 0u: {
            position = vec2<f32>(-1.0, -1.0);
        }
        case 1u: {
            position = vec2<f32>(1.0, -1.0);
        }
        case 2u: {
            position = vec2<f32>(-1.0, 1.0);
        }

        // 第二个三角形，与第一个构成一个覆盖全屏的矩形
        case 3u: {
            position = vec2<f32>(-1.0, 1.0);
        }
        case 4u: {
            position = vec2<f32>(1.0, -1.0);
        }
        case 5u: {
            position = vec2<f32>(1.0, 1.0);
        }

        // 默认情况，理论上不会执行
        default: {
            position = vec2<f32>(0.0, 0.0);
        }
    }

    return vec4<f32>(position, 0.0, 1.0);
}