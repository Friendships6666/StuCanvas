// src/function/render/wgsl-generator.ts

import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';

/**
 * 根据公式动态生成WGSL片段着色器的代码
 * @param formulas 公式数组
 * @returns [函数定义部分, 函数求值部分]
 */
export function generateFragmentShader(formulas: any[]): [string, string] {
    if (formulas.length === 0) return ["", ""];

    const definitions = formulas.map((f, i) => {
        const wgslExpr = translateJsExpressionToWgsl(f.wgsl_expression);
        return `fn eval_F_${i}(x: f32, y: f32) -> f32 { return ${wgslExpr}; }`;
    }).join('\n');

    const evaluations = formulas.map((_, i) => {
        return `
// --- Function ${i} ---
{
    let h_x = dpdx(x);
    let h_y = dpdy(y);

    let F_x_plus = eval_F_${i}(x + h_x, y);
    let F_x_minus = eval_F_${i}(x - h_x, y);
    let F_y_plus = eval_F_${i}(x, y + h_y);
    let F_y_minus = eval_F_${i}(x, y - h_y);

    var df_dx = 0.0;
    if (h_x != 0.0) {
        df_dx = (F_x_plus - F_x_minus) / (2.0 * h_x);
    }

    var df_dy = 0.0;
    if (h_y != 0.0) {
        df_dy = (F_y_plus - F_y_minus) / (2.0 * h_y);
    }

    let grad_len = length(vec2(df_dx, df_dy));
    let F_center = eval_F_${i}(x, y);

    let norm_dist = abs(F_center) / max(grad_len, 0.0001);

    let alpha = smoothstep(target_world_width, 0.0, norm_dist);

    if (alpha > 0.0) {
        let func_color = functions.data[${i}].color;
        let effective_alpha = alpha * func_color.a;

        // Correct "A-over-B" alpha blending for pre-multiplied alpha.
        let blended_rgb = func_color.rgb * effective_alpha + final_color.rgb * (1.0 - effective_alpha);
        let blended_a = effective_alpha + final_color.a * (1.0 - effective_alpha);

        final_color = vec4<f32>(blended_rgb, blended_a);
    }
}
`;
    }).join('');

    return [definitions, evaluations];
}