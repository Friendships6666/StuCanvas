/*src/coordinate/rectangular/use/use-formulas.ts*/
import { derived, type Readable } from 'svelte/store';
import { create, all } from 'mathjs';
import hash from 'object-hash';
import { scaleOrdinal } from 'd3-scale';
import { schemeCategory10 } from 'd3-scale-chromatic';

const math = create(all);
const colorScale = scaleOrdinal(schemeCategory10);

export interface DrawableFormula {
    id: string;
    wgsl_expression: string;
    color: { r: number; g: number; b: number; a: number };
}

/**
 * 一个 Svelte "hook"，用于解析字符串公式并将其转换为可绘制的数据结构。
 * @param formulas - 包含数学公式字符串的 store。
 * @returns 一个 derived store，其值为一个包含可绘制公式对象的数组。
 */
export function useFormulas(formulas: Readable<string[]>): Readable<DrawableFormula[]> {
    // ✅ 修正: 移除 'set' 回调和初始值，直接从函数返回新值。
    return derived(formulas, $formulas => {
        return $formulas.map(f => {
            try {
                const formulaStr = f.trim();
                if (!formulaStr) return null;

                const eqIndex = formulaStr.indexOf('=');
                let combinedExpression: string;

                if (eqIndex !== -1) {
                    const leftPart = formulaStr.substring(0, eqIndex).trim();
                    const rightPart = formulaStr.substring(eqIndex + 1).trim();
                    combinedExpression = `(${leftPart}) - (${rightPart})`;
                } else {
                    combinedExpression = `y - (${formulaStr})`;
                }

                const ast = math.parse(combinedExpression);

                const wgslExpression = ast.toString({
                    parenthesis: 'all',
                    handler: (node: any, options: any) => {
                        if (node.isConstantNode && typeof node.value === 'number') {
                            return Number.isInteger(node.value) ? node.value.toFixed(1) : node.value.toString();
                        }
                        if (node.isOperatorNode && node.op === '^') {
                            const base = node.args[0].toString(options);
                            const exponent = node.args[1].toString(options);
                            const exponentNum = parseFloat(exponent);
                            if (Number.isFinite(exponentNum) && Number.isInteger(exponentNum)) {
                                return (exponentNum % 2 !== 0)
                                    ? `(sign(${base}) * pow(abs(${base}), ${exponent}))`
                                    : `pow(abs(${base}), ${exponent})`;
                            }
                            return `pow(${base}, ${exponent})`;
                        }
                        if (node.isFunctionNode && node.name === 'log') {
                            const value = node.args[0].toString(options);
                            return node.args.length === 2
                                ? `(log(${value}) / log(${node.args[1].toString(options)}))`
                                : `log(${value})`;
                        }
                    }
                });

                const hexColor = colorScale(formulaStr);
                const r = parseInt(hexColor.slice(1, 3), 16) / 255;
                const g = parseInt(hexColor.slice(3, 5), 16) / 255;
                const b = parseInt(hexColor.slice(5, 7), 16) / 255;

                return {
                    id: hash(formulaStr),
                    wgsl_expression: wgslExpression,
                    color: { r, g, b, a: 0.9 }
                };
            } catch (e) {
                console.warn(`公式 "${f}" 解析失败:`, e);
                return null;
            }
        }).filter((f): f is DrawableFormula => f !== null);
    });
}