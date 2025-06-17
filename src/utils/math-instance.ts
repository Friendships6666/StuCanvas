/*src/utils/math-instance.ts*/
import {
    create, all,
    type MathNode,
    type SymbolNode,
    type FunctionNode,
    OperatorNode,
    ConstantNode
} from 'mathjs';

// 1. 创建一个 math.js 实例
const math = create(all);

// 2. 在这个实例上，一次性地、正确地扩展求导规则
math.import({
    derivative: {
        atan2: function (node: FunctionNode, options: { variable: SymbolNode }): MathNode {
            const y = node.args[0];
            const x = node.args[1];
            const variable = options.variable;

            const x2 = new OperatorNode('^', 'pow', [x, new ConstantNode(2)]);
            const y2 = new OperatorNode('^', 'pow', [y, new ConstantNode(2)]);
            const denominator = new OperatorNode('+', 'add', [x2, y2]);

            if (variable.name === 'y') {
                return new OperatorNode('/', 'divide', [x, denominator]);
            }
            if (variable.name === 'x') {
                const numerator = new OperatorNode('-', 'unaryMinus', [y]);
                return new OperatorNode('/', 'divide', [numerator, denominator]);
            }
            return new ConstantNode(0);
        }
    }
}, { override: true });

// 3. 导出这个已经配置好的唯一实例
export { math };