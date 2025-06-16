/* src/coordinate/rectangular/use/use-formulas.ts */
import { derived, type Readable } from 'svelte/store';
import hash from 'object-hash';
import { scaleOrdinal } from 'd3-scale';
import { schemeCategory10 } from 'd3-scale-chromatic';

// ✅ **修正**: 不再需要 mathjs，此文件不再负责复杂的解析
const colorScale = scaleOrdinal(schemeCategory10);

export interface DrawableFormula {
    id: string;
    // ✅ **修正**: wgsl_expression 现在就是一个纯粹的、未处理的数学表达式
    wgsl_expression: string;
    color: { r: number; g: number; b: number; a: number };
}

/**
 * 一个 Svelte "hook"，用于将用户输入的字符串转换为标准化的可绘制数据结构。
 * @param formulas - 包含用户输入的原始公式字符串的 store。
 * @returns 一个 derived store，其值为一个包含可绘制公式对象的数组。
 */
export function useFormulas(formulas: Readable<string[]>): Readable<DrawableFormula[]> {
    return derived(formulas, $formulas => {
        return $formulas.map(f => {
            const formulaStr = f.trim();
            if (!formulaStr) return null;

            // --- 核心逻辑 ---
            // 只进行简单的字符串处理，将所有公式统一为 "左侧 - (右侧)" 的隐函数形式
            let combinedExpression: string;
            const eqIndex = formulaStr.indexOf('=');

            if (eqIndex !== -1) {
                // 处理 "y = x^2" 这种显式形式
                const leftPart = formulaStr.substring(0, eqIndex).trim();
                const rightPart = formulaStr.substring(eqIndex + 1).trim();
                combinedExpression = `(${leftPart}) - (${rightPart})`;
            } else {
                // 处理 "x^2 + y^2 - 4" 这种隐式形式 (默认它等于0) 或 "x^2" (默认是 y=x^2)
                // ✅ **修正**: 如果表达式中没有 'y'，则假定它是 y=f(x) 的简写
                if (!/\by\b/.test(formulaStr)) {
                    combinedExpression = `y - (${formulaStr})`;
                } else {
                    combinedExpression = formulaStr;
                }
            }

            // --- 分配颜色和 ID ---
            const hexColor = colorScale(formulaStr);
            const r = parseInt(hexColor.slice(1, 3), 16) / 255;
            const g = parseInt(hexColor.slice(3, 5), 16) / 255;
            const b = parseInt(hexColor.slice(5, 7), 16) / 255;

            // ✅ **修正**: 返回的 wgsl_expression 是一个纯净的字符串，
            // 没有任何 sign(), pow() 或其他污染。
            // 真正的转换将由下游的 `wgsl-translator.ts` 统一处理。
            return {
                id: hash(formulaStr),
                wgsl_expression: combinedExpression,
                color: { r, g, b, a: 0.9 }
            };

        }).filter((f): f is DrawableFormula => f !== null);
    });
}