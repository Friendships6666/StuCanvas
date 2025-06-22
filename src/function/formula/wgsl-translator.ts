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

export function astToWgsl(node: MathNode): string {
    if (isConstantNode(node)) {
        return Number.isInteger(node.value) ? node.value.toFixed(1) : node.value.toString();
    }
    if (isSymbolNode(node)) {
        if (node.name.toLowerCase() === 'pi') return '3.1415926535';
        return node.name;
    }
    if (isFunctionNode(node)) {
        const args = node.args.map(arg => astToWgsl(arg));
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
                return `${functionName}(${args.join(', ')})`;
        }
    }
    if (isOperatorNode(node)) {
        const args = node.args.map(arg => astToWgsl(arg));
        if (node.op === '^') {
            const base = args[0];
            const exponentNode = node.args[1];

            if (isConstantNode(exponentNode)) {
                const exponentValue = exponentNode.value;
                if (Number.isInteger(exponentValue)) {
                    return (exponentValue % 2 !== 0)
                        ? `(sign(${base}) * pow(abs(${base}), ${exponentValue.toFixed(1)}))`
                        : `pow(abs(${base}), ${exponentValue.toFixed(1)})`;
                } else {
                    return `pow(abs(${base}), ${exponentValue})`;
                }
            } else {
                return `pow(abs(${base}), ${args[1]})`;
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