import { derived, type Readable } from 'svelte/store';
// 导入我们全新的、结构化的数据类型
import type { FormulaEntry } from '../../../stores/ui';
import {
    create, all,
    isConstantNode,
    isOperatorNode,
    isSymbolNode,
    isAssignmentNode
} from 'mathjs';
import type { MathNode } from 'mathjs';
import hash from 'object-hash';
import { scaleOrdinal } from 'd3-scale';
import { schemeCategory10 } from 'd3-scale-chromatic';

const math = create(all);
const colorScale = scaleOrdinal(schemeCategory10);

export interface DomainInterval {
    min: number;
    max: number;
}

export interface DrawableFormula {
    id: string;
    wgsl_expression: string;
    color: { r: number; g: number; b: number; a: number };
    domain: DomainInterval[] | null;
}

function refactorExponential(node: MathNode): string | null {
    if (isAssignmentNode(node)) {
        const leftSide = node.object;
        const rightSide = node.value;
        if (isSymbolNode(leftSide) && leftSide.name === 'y' && isOperatorNode(rightSide) && rightSide.op === '^') {
            const baseNode = rightSide.args[0];
            if (isConstantNode(baseNode)) {
                const base = baseNode.value;
                const exponentExpression = rightSide.args[1].toString();
                if (base > 0) {
                    const logBase = Math.log(base);
                    return `log(y) - ((${exponentExpression}) * ${logBase})`;
                } else {
                    return `y - (${rightSide.toString()})`;
                }
            }
        }
    }
    return null;
}

function mergeIntervals(intervals: DomainInterval[]): DomainInterval[] {
    if (intervals.length <= 1) return intervals;
    const sorted = intervals.sort((a, b) => a.min - b.min);
    const merged: DomainInterval[] = [sorted[0]];
    for (let i = 1; i < sorted.length; i++) {
        const lastMerged = merged[merged.length - 1];
        const current = sorted[i];
        if (current.min <= lastMerged.max) {
            lastMerged.max = Math.max(lastMerged.max, current.max);
        } else {
            merged.push(current);
        }
    }
    return merged;
}

export function useFormulas(formulas: Readable<FormulaEntry[]>): Readable<DrawableFormula[]> {
    return derived(formulas, $formulas => {
        return $formulas.filter(f => f.enabled).map(f => {
            try {
                // ✅ *** 核心修复：直接从结构化数据中获取表达式和定义域 ***
                const coreExpression = f.expression.trim();
                if (!coreExpression) return null;

                let domain: DomainInterval[] | null = null;

                // 1. 直接遍历 f.domains 数组，不再需要任何字符串分割
                if (f.domains && f.domains.length > 0) {
                    const parsedDomains: DomainInterval[] = [];
                    for (const d of f.domains) {
                        const trimmedCond = d.rawString.trim();
                        let match;
                        if ((match = trimmedCond.match(/(-?\d+\.?\d*)\s*<\s*x\s*<\s*(-?\d+\.?\d*)/))) {
                            parsedDomains.push({ min: parseFloat(match[1]), max: parseFloat(match[2]) });
                        } else if ((match = trimmedCond.match(/x\s*>\s*(-?\d+\.?\d*)/))) {
                            parsedDomains.push({ min: parseFloat(match[1]), max: Number.POSITIVE_INFINITY });
                        } else if ((match = trimmedCond.match(/(-?\d+\.?\d*)\s*<\s*x/))) {
                            parsedDomains.push({ min: parseFloat(match[1]), max: Number.POSITIVE_INFINITY });
                        } else if ((match = trimmedCond.match(/x\s*<\s*(-?\d+\.?\d*)/))) {
                            parsedDomains.push({ min: Number.NEGATIVE_INFINITY, max: parseFloat(match[1]) });
                        } else if ((match = trimmedCond.match(/(-?\d+\.?\d*)\s*>\s*x/))) {
                            parsedDomains.push({ min: Number.NEGATIVE_INFINITY, max: parseFloat(match[1]) });
                        }
                    }

                    if (parsedDomains.length > 0) {
                        domain = mergeIntervals(parsedDomains);
                    }
                }

                // 2. 对干净的 coreExpression 进行后续处理
                const node: MathNode = math.parse(coreExpression);
                let combinedExpression: string;

                const refactored = refactorExponential(node);

                if (refactored !== null) {
                    combinedExpression = refactored;
                } else {
                    const eqIndex = coreExpression.indexOf('=');
                    if (eqIndex !== -1) {
                        const leftPart = coreExpression.substring(0, eqIndex).trim();
                        const rightPart = coreExpression.substring(eqIndex + 1).trim();
                        combinedExpression = `(${leftPart}) - (${rightPart})`;
                    } else {
                        if (!/\by\b/.test(coreExpression)) {
                            combinedExpression = `y - (${coreExpression})`;
                        } else {
                            combinedExpression = coreExpression;
                        }
                    }
                }

                const hexColor = colorScale(f.expression);
                const r = parseInt(hexColor.slice(1, 3), 16) / 255;
                const g = parseInt(hexColor.slice(3, 5), 16) / 255;
                const b = parseInt(hexColor.slice(5, 7), 16) / 255;

                return {
                    id: f.id,
                    wgsl_expression: combinedExpression,
                    color: { r, g, b, a: 0.9 },
                    domain: domain
                };
            } catch (e) {
                console.warn(`公式 "${f.expression}" 解析失败:`, e);
                return null;
            }
        }).filter((f): f is DrawableFormula => f !== null);
    });
}