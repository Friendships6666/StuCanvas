/*src/coordinate/rectangular/use/use-formulas.ts*/
import { derived, type Readable } from 'svelte/store';
import type { FormulaEntry } from '../../../stores/ui';
// 导入共享的、已经配置好的 math 实例
import { math } from '../../../function/formula/math-instance'; // 假设路径为 ../utils/math-instance
import {
    isAssignmentNode,
    isSymbolNode,
    type MathNode,
} from 'mathjs';
import { scaleOrdinal } from 'd3-scale';
import { schemeCategory10 } from 'd3-scale-chromatic';

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
                const coreExpression = f.expression.trim();
                if (!coreExpression) return null;

                let domain: DomainInterval[] | null = null;

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

                let expressionToParse: string;
                const eqIndex = coreExpression.indexOf('=');

                if (eqIndex !== -1) {
                    const leftPart = coreExpression.substring(0, eqIndex).trim();
                    const rightPart = coreExpression.substring(eqIndex + 1).trim();

                    // 处理不完整的输入，如 "y ="
                    if (!rightPart) {
                        throw new Error("Equation has no right-hand side.");
                    }
                    expressionToParse = `(${leftPart}) - (${rightPart})`;
                } else {
                    const tempNode = math.parse(coreExpression);
                    if (isAssignmentNode(tempNode) && isSymbolNode(tempNode.object) && tempNode.object.name === 'y') {
                        expressionToParse = `(y) - (${tempNode.value.toString()})`;
                    } else if (!/\by\b/.test(coreExpression)) {
                        expressionToParse = `y - (${coreExpression})`;
                    } else {
                        expressionToParse = coreExpression;
                    }
                }

                const hexColor = colorScale(f.expression);
                const r = parseInt(hexColor.slice(1, 3), 16) / 255;
                const g = parseInt(hexColor.slice(3, 5), 16) / 255;
                const b = parseInt(hexColor.slice(5, 7), 16) / 255;

                return {
                    id: f.id,
                    wgsl_expression: expressionToParse,
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