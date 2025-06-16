import {
    parse,
    derivative,
    isConstantNode,
    isSymbolNode,
    isOperatorNode,
    isFunctionNode,
    isParenthesisNode
} from 'mathjs';
import type { MathNode } from 'mathjs';

/**
 * 一个完整的、递归的 AST 遍历器，负责将 math.js 的 AST 节点转换为 WGSL 代码字符串。
 * 此版本使用了完整的类型守卫和正确的属性访问，以保证 TypeScript 的静态类型安全。
 */
function astToWgsl(node: MathNode): string {
    if (isConstantNode(node)) {
        return Number.isInteger(node.value) ? node.value.toFixed(1) : node.value.toString();
    }
    if (isSymbolNode(node)) {
        if (node.name.toLowerCase() === 'pi') return '3.1415926535';
        return node.name;
    }
    if (isFunctionNode(node)) {
        const args = node.args.map(arg => astToWgsl(arg));

        // ✅ 核心修复：访问 node.fn.name 而不是 node.name
        const functionName = node.fn.name;

        switch (functionName) {
            case 'log':
                if (args.length === 2) {
                    return `(log(${args[0]}) / log(${args[1]}))`;
                }
                return `log(${args[0]})`;
            case 'log10':
                return `(log(${args[0]}) / log(10.0))`;
            case 'log2':
                return `(log(${args[0]}) / log(2.0))`;
            default:
                // ✅ 核心修复：使用正确的 functionName
                return `${functionName}(${args.join(', ')})`;
        }
    }
    if (isOperatorNode(node)) {
        const args = node.args.map(arg => astToWgsl(arg));
        if (node.op === '^') {
            const base = args[0];
            const exponentNode = node.args[1];
            let exponentValue: number;
            try {
                exponentValue = exponentNode.evaluate();
            } catch (e) {
                return `pow(abs(${base}), ${args[1]})`;
            }
            if (Number.isInteger(exponentValue)) {
                return (exponentValue % 2 !== 0)
                    ? `(sign(${base}) * pow(abs(${base}), ${exponentValue.toFixed(1)}))`
                    : `pow(abs(${base}), ${exponentValue.toFixed(1)})`;
            } else {
                const CUBE_ROOT_EPSILON = 0.001;
                const invExp = 1 / exponentValue;
                if (Math.abs(Math.round(invExp) % 2) === 1 && Math.abs(invExp - Math.round(invExp)) < CUBE_ROOT_EPSILON) {
                    return `(sign(${base}) * pow(abs(${base}), ${exponentValue}))`;
                }
                return `pow(abs(${base}), ${exponentValue})`;
            }
        }
        if (args.length === 2) {
            return `(${args[0]} ${node.op} ${args[1]})`;
        }
        if (args.length === 1) {
            return `(${node.op}${args[0]})`;
        }
    }
    if (isParenthesisNode(node)) {
        return `(${astToWgsl(node.content)})`;
    }

    throw new Error(`Unsupported node type in astToWgsl: ${'type' in node ? node.type : 'unknown'}`);
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
    return astToWgsl(expressionNode);
}