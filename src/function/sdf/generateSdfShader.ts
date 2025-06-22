import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';
import { math } from '../../utils/math-instance';
import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';
import type { MathNode } from 'mathjs';

export function generateSdfShader(formulas: DrawableFormula[]): [string, string] {
    if (!formulas || formulas.length === 0) {
        return ["", ""];
    }

    const definitions = formulas.map((f, i) => {
        try {
            const originalNode: MathNode = math.parse(f.wgsl_expression);
            const dFdxNode: MathNode = math.derivative(originalNode, 'x');
            const dFdyNode: MathNode = math.derivative(originalNode, 'y');
            const d2Fdx2Node: MathNode = math.derivative(dFdxNode, 'x');
            const d2Fdy2Node: MathNode = math.derivative(dFdyNode, 'y');
            const d2FxyNode: MathNode = math.derivative(dFdxNode, 'y');

            const wgslExpr = translateJsExpressionToWgsl(originalNode.toString());
            const wgslDfdxExpr = translateJsExpressionToWgsl(dFdxNode.toString());
            const wgslDfdyExpr = translateJsExpressionToWgsl(dFdyNode.toString());
            const wgslD2fdx2Expr = translateJsExpressionToWgsl(d2Fdx2Node.toString());
            const wgslD2fdy2Expr = translateJsExpressionToWgsl(d2Fdy2Node.toString());
            const wgslD2fxyExpr = translateJsExpressionToWgsl(d2FxyNode.toString());

            return `
fn eval_F_${i}(x: f32, y: f32) -> f32 { return ${wgslExpr}; }
fn eval_dFdx_${i}(x: f32, y: f32) -> f32 { return ${wgslDfdxExpr}; }
fn eval_dFdy_${i}(x: f32, y: f32) -> f32 { return ${wgslDfdyExpr}; }
fn eval_d2Fdx2_${i}(x: f32, y: f32) -> f32 { return ${wgslD2fdx2Expr}; }
fn eval_d2Fdy2_${i}(x: f32, y: f32) -> f32 { return ${wgslD2fdy2Expr}; }
fn eval_d2Fxy_${i}(x: f32, y: f32) -> f32 { return ${wgslD2fxyExpr}; }
            `;
        } catch (e) {
            return `
fn eval_F_${i}(x: f32, y: f32) -> f32 { return 1.0e38; }
fn eval_dFdx_${i}(x: f32, y: f32) -> f32 { return 0.0; }
fn eval_dFdy_${i}(x: f32, y: f32) -> f32 { return 0.0; }
fn eval_d2Fdx2_${i}(x: f32, y: f32) -> f32 { return 0.0; }
fn eval_d2Fdy2_${i}(x: f32, y: f32) -> f32 { return 0.0; }
fn eval_d2Fxy_${i}(x: f32, y: f32) -> f32 { return 0.0; }
            `;
        }
    }).join('\n');

    const evaluations = formulas.map((f, i) => {
        return `
{
    const SAFE_F32_MAX = 3.0e38;
    let F_center = eval_F_${i}(x, y);
    let Fx = eval_dFdx_${i}(x, y);
    let Fy = eval_dFdy_${i}(x, y);
    if (abs(F_center) < SAFE_F32_MAX && abs(Fx) < SAFE_F32_MAX && abs(Fy) < SAFE_F32_MAX) {
        let grad_len_sq = Fx * Fx + Fy * Fy;
        if (grad_len_sq > 1.0e-19) {
            let Fxx = eval_d2Fdx2_${i}(x, y);
            let Fyy = eval_d2Fdy2_${i}(x, y);
            let Fxy = eval_d2Fxy_${i}(x, y);
            if (abs(Fxx) < SAFE_F32_MAX && abs(Fyy) < SAFE_F32_MAX && abs(Fxy) < SAFE_F32_MAX) {
                let grad_len = sqrt(grad_len_sq);
                let K_numerator = Fxx * Fy * Fy - 2.0 * Fxy * Fx * Fy + Fyy * Fx * Fx;
                let K = K_numerator / (grad_len_sq * grad_len);
                let discriminant = 1.0 - 2.0 * K * F_center;
                if (discriminant >= 0.0) {
                    let dist = grad_len * (1.0 - sqrt(discriminant)) / max(abs(K), 1.0e-9);
                    var final_alpha = smoothstep(target_world_width, 0.0, abs(dist));
                    if (final_alpha > 0.0) {
                        let func_info = functions.data[${i}];
                        let current_color = vec4(func_info.color.rgb, func_info.color.a * final_alpha);
                        final_color = blend_colors(final_color, current_color);
                    }
                }
            }
        }
    }
}
`;
    }).join('');

    const finalEvaluations = `
let target_world_width = length(vec2(dpdx(x), dpdy(y)));
${evaluations}
    `;

    return [definitions, finalEvaluations];
}