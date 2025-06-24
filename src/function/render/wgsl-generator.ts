/*src/function/render/wgsl-generator.ts*/
import { math } from '../../utils/math-instance';
import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';
import type { MathNode } from 'mathjs';

interface DrawableFormula {
    id: string;
    wgsl_expression: string;
    color: { r: number; g: number; b: number; a: number };
    domain: { min: number; max: number }[] | null;
}

export function generateFragmentShader(formulas: DrawableFormula[], clipOffscreen: boolean): [string, string] {
    if (!formulas || formulas.length === 0) {
        return ["", ""];
    }

    // 阶段 1: 生成函数定义 (这部分是正确的，无需修改)
    const definitions = formulas.map((f, i) => {
        try {
            const originalNode: MathNode = math.parse(f.wgsl_expression);
            const dFdxNode: MathNode = math.simplify(math.derivative(originalNode, 'x'));
            const dFdyNode: MathNode = math.simplify(math.derivative(originalNode, 'y'));
            const d2Fdx2Node: MathNode = math.simplify(math.derivative(math.derivative(originalNode, 'x'), 'x'));
            const d2Fdy2Node: MathNode = math.simplify(math.derivative(math.derivative(originalNode, 'y'), 'y'));
            const d2FxyNode: MathNode = math.simplify(math.derivative(math.derivative(originalNode, 'x'), 'y'));

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

    // 阶段 2: 生成求值逻辑
    const evaluations = formulas.map((f, i) => {
        // ✅ *** 核心修复 ***
        // 我们为有定义域和无定义域的两种情况创建完全独立的 if/else 代码路径。
        // 这种方法虽然有代码重复，但可以彻底消除 TypeScript 编译器的类型歧义。
        if (f.domain && f.domain.length > 0) {
            // ==================================================
            // 路径 1: 公式有定义域
            // 在这个 'if' 块内部, TypeScript 知道 f.domain 不是 null。
            // ==================================================
            const domain = f.domain; // 创建一个类型绝对安全的常量。

            const conditions = domain.map((interval) => {
                const minCheck = (interval.min !== -Infinity) ? `x > ${interval.min.toFixed(4)}` : "true";
                const maxCheck = (interval.max !== Infinity) ? `x < ${interval.max.toFixed(4)}` : "true";
                return `(${minCheck} && ${maxCheck})`;
            });
            const domainCondition = `(${conditions.join(' || ')})`;

            const boundaryAlphaCalculation = `
                var boundary_alphas = array<f32, ${domain.length}>();
                ${domain.map((interval, j) => {
                let alphaMin = "1.0";
                let alphaMax = "1.0";
                if (interval.min !== -Infinity) {
                    alphaMin = `smoothstep(0.0, 2.0 * target_world_width, x - ${interval.min.toFixed(4)})`;
                }
                if (interval.max !== Infinity) {
                    alphaMax = `smoothstep(0.0, 2.0 * target_world_width, ${interval.max.toFixed(4)} - x)`;
                }
                return `boundary_alphas[${j}] = min(${alphaMin}, ${alphaMax});`;
            }).join('\n')}
                var boundary_alpha = 0.0;
                for (var k = 0; k < ${domain.length}; k = k + 1) {
                    boundary_alpha = max(boundary_alpha, boundary_alphas[k]);
                }
                final_alpha = final_alpha * boundary_alpha;
            `;

            // 直接返回为“有定义域”情况生成的完整 WGSL 代码块
            return `
{
    if ${domainCondition} {
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
                    let K_numerator = Fxx * Fx * Fx + 2.0 * Fxy * Fx * Fy + Fyy * Fy * Fy;
                    let K = K_numerator / grad_len_sq;
                    let discriminant = grad_len_sq - 2.0 * K * F_center;
                    if (discriminant >= 0.0) {
                        let denominator = grad_len + sqrt(discriminant);
                        let final_dist = abs(23.0 * F_center / max(abs(denominator), 1.0e-9));
                        var final_alpha = smoothstep(target_world_width, 0.0, final_dist);
                        ${boundaryAlphaCalculation}
                        if (final_alpha > 0.0) {
                            let func_color = functions.data[${i}];
                            let current_color = vec4(func_color.rgb, func_color.a * final_alpha);
                            final_color = blend_colors(final_color, current_color);
                        }
                    }
                }
            }
        }
    }
}
`;
        } else {
            // ==================================================
            // 路径 2: 公式没有定义域
            // ==================================================

            // 直接返回为“无定义域”情况生成的完整 WGSL 代码块
            return `
{
    if true {
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
                    let K_numerator = Fxx * Fx * Fx + 2.0 * Fxy * Fx * Fy + Fyy * Fy * Fy;
                    let K = K_numerator / grad_len_sq;
                    let discriminant = grad_len_sq - 2.0 * K * F_center;
                    if (discriminant >= 0.0) {
                        let denominator = grad_len + sqrt(discriminant);
                        let final_dist = abs(2.0 * F_center / max(abs(denominator), 1.0e-9));
                        var final_alpha = smoothstep(target_world_width, 0.0, final_dist);
                        
                        if (final_alpha > 0.0) {
                            let func_color = functions.data[${i}];
                            let current_color = vec4(func_color.rgb, func_color.a * final_alpha);
                            final_color = blend_colors(final_color, current_color);
                        }
                    }
                }
            }
        }
    }
}
`;
        }
    }).join('');

    // 阶段 3: 组合最终代码
    const finalEvaluations = `
    let target_world_width = length(vec2(dpdx(x), dpdy(y)));
    ${clipOffscreen ? `let y_max_world = uniforms.clip_params.x; if (y > y_max_world) { discard; }` : ''}
    ${evaluations}
    `;

    return [definitions, finalEvaluations];
}