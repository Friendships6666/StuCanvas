import { parse, derivative } from 'mathjs';
import type { MathNode } from 'mathjs';

/**
 * 一个独立的、健壮的辅助函数，专门用于处理乘方节点。
 */
function handlePowerNode(node: MathNode, options: any): string | undefined {
    // 确保我们只处理乘方操作符节点
    if (node.type !== 'OperatorNode' || node.op !== '^') {
        return undefined; // 对于其他节点，让默认 handler 处理
    }

    const base = node.args[0].toString(options);
    const exponentNode = node.args[1];

    // 情况 1：指数是一个常量数字
    if (exponentNode.isConstantNode) {
        const exponentValue = exponentNode.value;

        if (Number.isInteger(exponentValue)) {
            // 正奇数指数: 保留符号
            if (exponentValue % 2 !== 0) {
                return `(sign(${base}) * pow(abs(${base}), ${exponentValue.toFixed(1)}))`;
            }
            // 正偶数指数: 结果为正
            else {
                return `pow(abs(${base}), ${exponentValue.toFixed(1)})`;
            }
        } else {
            // 分数/小数指数: 假定结果为正（渲染第二象限的关键）
            return `pow(abs(${base}), ${exponentValue.toString()})`;
        }
    }
    // 情况 2：指数是变量或复杂表达式
    else {
        const exponent = exponentNode.toString(options);
        // 退回到最安全的模式，假定底数为正
        return `pow(abs(${base}), ${exponent})`;
    }
}


export function translateJsExpressionToWgsl(jsExpr: string, derivativeOf?: 'x' | 'y'): string {
    let expressionNode: MathNode;

    try {
        expressionNode = parse(jsExpr);
        if (derivativeOf) {
            expressionNode = derivative(expressionNode, derivativeOf);
        }
    } catch (error) {
        console.error(`Failed to parse or differentiate "${jsExpr}":`, error);
        return "(0.0)";
    }

    let finalWgslExpr = expressionNode.toString({
        // ✅ 核心修复：现在 handler 只委托给我们的专业辅助函数
        handler: handlePowerNode
    });

    finalWgslExpr = finalWgslExpr.replace(/(?<![\w.])(\d+)(?![.\w])/g, '$1.0');
    finalWgslExpr = finalWgslExpr.replace(/\bpi\b/g, '3.1415926535');
    finalWgslExpr = finalWgslExpr.replace(/\blog10\s*\(([^)]+)\)/g, '(log($1)/log(10.0))');
    finalWgslExpr = finalWgslExpr.replace(/\blog2\s*\(([^)]+)\)/g, '(log($1)/log(2.0))');
    finalWgslExpr = finalWgslExpr.replace(/log\(([^,]+?),\s*([^)]+?)\)/g, '(log($1) / log($2))');

    return `(${finalWgslExpr})`;
}