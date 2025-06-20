/*src/function/renderCore/wgsl-generator.ts*/
import { math } from '../../utils/math-instance'; // 导入共享的、已经配置好的 math 实例
import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';
import type { MathNode } from 'mathjs';

export function generateFragmentShader(formulas: any[], clipOffscreen: boolean): [string, string] {
    if (!formulas || formulas.length === 0) {
        return ["", ""];
    }

    const definitions = formulas.map((f, i) => {
        try {
            // 使用共享实例的 parse 和 derivative 方法
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
            console.error(`公式 "${f.wgsl_expression}" 求导或翻译失败:`, e);
            return `
                fn eval_F_${i}(x: f32, y: f32) -> f32 { return 1.0e38; } fn eval_dFdx_${i}(x: f32, y: f32) -> f32 { return 0.0; } fn eval_dFdy_${i}(x: f32, y: f32) -> f32 { return 0.0; }
                fn eval_d2Fdx2_${i}(x: f32, y: f32) -> f32 { return 0.0; } fn eval_d2Fdy2_${i}(x: f32, y: f32) -> f32 { return 0.0; } fn eval_d2Fxy_${i}(x: f32, y: f32) -> f32 { return 0.0; }
            `;
        }
    }).join('\n');

    const evaluations = formulas.map((f, i) => {
        const hasDomain = f.domain && f.domain.length > 0;
        let domainCondition = "true";

        if (hasDomain) {
            const conditions = f.domain.map((interval: any) => {
                const minCheck = (interval.min !== -Infinity) ? `x > ${interval.min.toFixed(4)}` : "true";
                const maxCheck = (interval.max !== Infinity) ? `x < ${interval.max.toFixed(4)}` : "true";
                return `(${minCheck} && ${maxCheck})`;
            });
            domainCondition = `(${conditions.join(' || ')})`;
        }

        const boundaryAlphaCalculation = hasDomain ? `
                var boundary_alphas = array<f32, ${f.domain.length}>();
                ${f.domain.map((interval: any, j: number) => {
            let alphaMin = "1.0";
            let alphaMax = "1.0";
            if (interval.min !== -Infinity) {
                alphaMin = `smoothstep(0.0, 2.0, (x - ${interval.min.toFixed(4)}) / target_world_width)`;
            }
            if (interval.max !== Infinity) {
                alphaMax = `smoothstep(0.0, 2.0, (${interval.max.toFixed(4)} - x) / target_world_width)`;
            }
            return `boundary_alphas[${j}] = min(${alphaMin}, ${alphaMax});`;
        }).join('\n')}
                
                var boundary_alpha = 0.0;
                for (var k = 0; k < ${f.domain.length}; k = k + 1) {
                    boundary_alpha = max(boundary_alpha, boundary_alphas[k]);
                }
                final_alpha = final_alpha * boundary_alpha;
                ` : '';

        return `
{
    if ${domainCondition} {
        const SAFE_F32_MAX = 3.0e38;
        let F_center = eval_F_${i}(x, y);
        let Fx = eval_dFdx_${i}(x, y);
        let Fy = eval_dFdy_${i}(x, y);

        if (abs(F_center) < SAFE_F32_MAX && abs(Fx) < SAFE_F32_MAX && abs(Fy) < SAFE_F32_MAX) {
            
            // --- FMA-Proof Calculation of grad_len_sq ---
            let fx_sq = Fx * Fx;
            let fy_sq = Fy * Fy;
            let grad_len_sq = fx_sq + fy_sq;
            
            if (grad_len_sq > 1.0e-19) {
                var final_alpha = 0.0;
                
                let Fxx = eval_d2Fdx2_${i}(x, y);
                let Fyy = eval_d2Fdy2_${i}(x, y);
                let Fxy = eval_d2Fxy_${i}(x, y);

                if (abs(Fxx) < SAFE_F32_MAX && abs(Fyy) < SAFE_F32_MAX && abs(Fxy) < SAFE_F32_MAX) {
                    let grad_len = sqrt(grad_len_sq);

                    // --- FMA-Proof Calculation of K_numerator ---
                    let k_term1_a = Fx * Fx;
                    let k_term1 = Fxx * k_term1_a;
                    let k_term2_a = 2.0 * Fxy;
                    let k_term2_b = k_term2_a * Fx;
                    let k_term2 = k_term2_b * Fy;
                    let k_term3_a = Fy * Fy;
                    let k_term3 = Fyy * k_term3_a;
                    let k_sum1 = k_term1 + k_term2;
                    let K_numerator = k_sum1 + k_term3;
                    
                    let K = K_numerator / grad_len_sq;

                    // --- FMA-Proof Calculation of discriminant ---
                    let disc_term_a = 2.0 * K;
                    let disc_term_b = disc_term_a * F_center;
                    let discriminant = grad_len_sq - disc_term_b;

                    if (discriminant >= 0.0) {
                        // --- SAFE REGION ---
                        let denominator = grad_len + sqrt(discriminant);
                        let final_dist = abs(2.0 * F_center / max(abs(denominator), 1.0e-9));
                        final_alpha = smoothstep(target_world_width, 0.0, final_dist);
                    }
                }
                
                ${boundaryAlphaCalculation}

                if (final_alpha > 0.0) {
                    let func_color = functions.data[${i}];
                    let effective_alpha = final_alpha * func_color.a;

                    // --- FMA-Proof Color Blending ---
                    let inv_alpha = 1.0 - effective_alpha;
                    let rgb_term1 = func_color.rgb * effective_alpha;
                    let rgb_term2 = final_color.rgb * inv_alpha;
                    let blended_rgb = rgb_term1 + rgb_term2;
                    let alpha_term = final_color.a * inv_alpha;
                    let blended_a = effective_alpha + alpha_term;

                    final_color = vec4<f32>(blended_rgb, blended_a);
                }
            }
        }
    }
}
`;
    }).join('');

    const finalEvaluations = `
    let target_world_width = length(vec2(dpdx(x), dpdy(y)));
    ${clipOffscreen ? `let y_max_world = uniforms.clip_params.x; if (y > y_max_world) { discard; }` : ''}
    ${evaluations}
    `;

    return [definitions, finalEvaluations];
}