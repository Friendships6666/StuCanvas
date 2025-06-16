import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';

export function generateFragmentShader(formulas: any[], clipOffscreen: boolean): [string, string] {
    if (!formulas || formulas.length === 0) {
        return ["", ""];
    }

    const definitions = formulas.map((f, i) => {
        const wgslExpr = translateJsExpressionToWgsl(f.wgsl_expression);
        const wgslDfdxExpr = translateJsExpressionToWgsl(f.wgsl_expression, 'x');
        const wgslDfdyExpr = translateJsExpressionToWgsl(f.wgsl_expression, 'y');
        return `
            fn eval_F_${i}(x: f32, y: f32) -> f32 { return ${wgslExpr}; }
            fn eval_dFdx_${i}(x: f32, y: f32) -> f32 { return ${wgslDfdxExpr}; }
            fn eval_dFdy_${i}(x: f32, y: f32) -> f32 { return ${wgslDfdyExpr}; }
        `;
    }).join('\n');

    let clippingCode = '';
    if (clipOffscreen) {
        clippingCode = `
    let y_max_world = uniforms.clip_params.x;
    if (y > y_max_world) {
        discard;
    }
    `;
    }

    const evaluations = formulas.map((_, i) => {
        return `
{
    let df_dx = eval_dFdx_${i}(x, y);
    let df_dy = eval_dFdy_${i}(x, y);
    let grad_len = length(vec2(df_dx, df_dy));
    let F_center = eval_F_${i}(x, y);
    let norm_dist = abs(F_center) / max(grad_len, 0.0001);
    let alpha = smoothstep(target_world_width, 0.0, norm_dist);

    if (alpha > 0.0) {
        let func_color = functions.data[${i}];
        let effective_alpha = alpha * func_color.a;
        let blended_rgb = func_color.rgb * effective_alpha + final_color.rgb * (1.0 - effective_alpha);
        let blended_a = effective_alpha + final_color.a * (1.0 - effective_alpha);
        final_color = vec4<f32>(blended_rgb, blended_a);
    }
}
`;
    }).join('');

    return [definitions, clippingCode + evaluations];
}