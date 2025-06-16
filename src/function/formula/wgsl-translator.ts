import { parse, derivative } from 'mathjs';
import type { MathNode } from 'mathjs';

// ✅ *** 核心修复：创建一个独立的、智能的 pow 函数转换器 ***
function handlePowerNode(node: MathNode, options: any): string | undefined {
    if (node.type === 'OperatorNode' && node.op === '^') {
        const base = node.args[0].toString(options);
        const exponentNode = node.args[1];
        let exponentValue: number;

        try {
            // 尝试对指数进行求值，这能处理像 "2/3" 或 "3" 这样的常量表达式
            exponentValue = exponentNode.evaluate();
        } catch (e) {
            // 如果指数是变量（如 x^y），则无法求值，只能使用标准 pow
            const exponent = exponentNode.toString(options);
            return `pow(abs(${base}), ${exponent})`;
        }

        // 如果指数是整数
        if (Number.isInteger(exponentValue)) {
            // 奇数指数 -> 保留符号
            if (exponentValue % 2 !== 0) {
                return `(sign(${base}) * pow(abs(${base}), ${exponentValue.toFixed(1)}))`;
            }
            // 偶数指数 -> 结果为正
            else {
                return `pow(abs(${base}), ${exponentValue.toFixed(1)})`;
            }
        }
        // 如果指数是分数/小数
        else {
            // 特殊处理奇数分母的分数，例如 x^(1/3) 或 x^(5/3)
            // 这是一个简化的检查，通过检查与常见根（如3,5,7）的接近程度
            // 这对于 `y=x^(1/3)` 的正确渲染至关重要
            const CUBE_ROOT_EPSILON = 0.001;
            if (Math.abs(Math.round(1 / exponentValue) % 2) === 1 && Math.abs(1/exponentValue - Math.round(1/exponentValue)) < CUBE_ROOT_EPSILON) {
                return `(sign(${base}) * pow(abs(${base}), ${exponentValue}))`;
            }

            // 对于其他所有分数（特别是偶数分子，如 2/3），我们假定结果为正
            // 这能正确渲染 y = x^(2/3) 在第二象限的图像
            return `pow(abs(${base}), ${exponentValue})`;
        }
    }
    return undefined;
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

    // ✅ **修正**：现在 `toString` 的 handler 只做一件事：委托给我们的智能 pow 处理器
    let finalWgslExpr = expressionNode.toString({
        handler: (node: MathNode, options: any) => handlePowerNode(node, options)
    });

    finalWgslExpr = finalWgslExpr.replace(/(?<![\w.])(\d+)(?![.\w])/g, '$1.0');
    finalWgslExpr = finalWgslExpr.replace(/\bpi\b/g, '3.1415926535');
    finalWgslExpr = finalWgslExpr.replace(/\blog10\s*\(([^)]+)\)/g, '(log($1)/log(10.0))');
    finalWgslExpr = finalWgslExpr.replace(/\blog2\s*\(([^)]+)\)/g, '(log($1)/log(2.0))');
    finalWgslExpr = finalWgslExpr.replace(/log\(([^,]+?),\s*([^)]+?)\)/g, '(log($1) / log($2))');

    return `(${finalWgslExpr})`;
}