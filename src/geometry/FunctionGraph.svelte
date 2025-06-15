<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { View } from '../stores/camera';
    import type { Renderable } from '../coordinate/rectangular/interaction/renderer';
    import vertexShaderCode from './function.implicit.vertex.wgsl?raw';
    import fragmentShaderTemplate from './function.implicit.fragment.wgsl?raw';
    import { create, all } from 'mathjs';

    const math = create(all); // 这是一个创建的 mathjs 实例。linter 警告可能是因为它直接调用 math.js 内部方法很少。

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: View;
    export let aspect: number;
    export let formula: { id: string; wgsl_expression: string }; // wgsl_expression 是 F(x,y) 的字符串
    export let requestRender: () => void;
    export let color = { r: 0.1, g: 0.4, b: 0.9, a: 1.0 };
    export let layer: number = 3;

    // --- GPU 资源 ---
    let renderPipeline: GPURenderPipeline | null = null;
    let uniformBuffer: GPUBuffer | null = null;
    let bindGroup: GPUBindGroup | null = null;
    let self: Renderable | null = null;

    function translateJsExpressionToWgsl(jsExpr: string): string {
        let wgslExpr = jsExpr.trim();

        // 1. 确保所有数值字面量都是浮点数 (例如 2 -> 2.0)。
        // 这一步应该非常靠前，在进行任何表达式转换之前。
        wgslExpr = wgslExpr.replace(/(?<![\w.])(\d+)(?![.\w])/g, '$1.0');

        // 2. 替换数学常数 pi
        wgslExpr = wgslExpr.replace(/\bpi\b/g, '3.1415926535');

        // 3. 将 math.js 特定的函数名或表达方式转换为 WGSL 等价形式
        wgslExpr = wgslExpr.replace(/\blog10\s*\(([^)]+)\)/g, '(log($1)/log(10.0))');
        wgslExpr = wgslExpr.replace(/\blog2\s*\(([^)]+)\)/g, '(log($1)/log(2.0))');
        // 对于 sin, cos, tan, atan, sqrt, log, exp, abs, ceil, floor, round 这些 WGSL 具有同名内置函数的，无需特殊处理。
        // wgslExpr = wgslExpr.replace(/\b(?:sin|cos|tan|atan|sqrt|log|exp|abs|ceil|floor|round)\b/g, (match) => { return match; });

        // 4. 将所有 `^` 运算符转换为 `pow()` 函数。
        // 这在数字字面量转换之后，`pow` 函数负数基底处理之前。
        wgslExpr = wgslExpr.replace(
            /([a-zA-Z_][a-zA-Z0-9_]*|\d+(?:\.\d*)?|\([^)]*\))\s*\^\s*([a-zA-Z_][a-zA-Z0-9_]*|\d+(?:\.\d*)?|\([^)]*\))/g,
            "pow($1, $2)"
        );

        // 5. 安全地转换 `pow(base, exponent)` 调用，处理负数基底的情况。
        // 这个步骤应在所有 `^` 被转换为 `pow` 之后执行。
        wgslExpr = wgslExpr.replace(/pow\s*\(([^,]+?)\s*,\s*([^)]+?)\s*\)/g, (match, base, exponentStr) => {
            const expNum = parseFloat(exponentStr.trim());

            if (Number.isFinite(expNum) && Number.isInteger(expNum)) {
                const expInt = Math.round(expNum); // 确保是整数

                if (expInt % 2 !== 0) { // 奇数整数指数
                    return `(sign(${base.trim()}) * pow(abs(${base.trim()}), ${exponentStr.trim()}))`;
                } else { // 偶数整数指数
                    return `pow(abs(${base.trim()}), ${exponentStr.trim()})`;
                }
            }
            // 对于非整数指数或不可解析的指数，直接返回原始 pow 表达式。
            return match;
        });

        // 6. 处理 WGSL 中没有内置但 math.js 可能生成的 `log(x, base)`
        wgslExpr = wgslExpr.replace(/log\(([^,]+?),\s*([^)]+?)\)/g, '(log($1) / log($2))');

        return wgslExpr;
    }

    // --- 初始化 ---
    // 确保 device 和 formula.wgsl_expression 都有效才触发初始化
    $: device && formula.wgsl_expression && initializePipeline();

    function initializePipeline() {
        // ✅ 早期检查：如果设备或公式表达式无效，则不进行初始化
        if (!device || !formula || typeof formula.wgsl_expression !== 'string' || formula.wgsl_expression.trim() === '') {
            console.warn("Skipping pipeline initialization: device or valid formula expression missing.");
            if (self) { // 如果已经存在，需要清理掉旧的渲染器
                register.delete(self);
                self = null;
            }
            uniformBuffer?.destroy();
            uniformBuffer = null; // 确保置空，防止后续尝试访问
            return; // 提前退出
        }

        // ✅ 防御性声明：将 functionBody 声明为 let 并在 try 块之外初始化
        let functionBody: string = "";

        // 清理旧资源（如果在重新初始化时）
        if (self) {
            register.delete(self);
            self = null;
        }
        uniformBuffer?.destroy(); // 销毁之前的 uniformBuffer
        uniformBuffer = null; // 确保置空

        try {
            uniformBuffer = device.createBuffer({
                label: `Uniforms for ${formula.id}`,
                size: 64, // Uniforms 结构体总大小
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });

            // ✅ 在这里赋值 functionBody
            functionBody = `return ${translateJsExpressionToWgsl(formula.wgsl_expression)};`;

            console.log("Generated WGSL functionBody:", functionBody); // 打印生成的 WGSL 代码，方便调试

            const finalFragmentCode = fragmentShaderTemplate.replace(
                /\/\*__WGSL_GRAPH_FUNCTION_BODY__\*\/.*\/\*__WGSL_GRAPH_FUNCTION_BODY__\*\//,
                functionBody
            );

            const vertexShaderModule = device.createShaderModule({ code: vertexShaderCode });
            const fragmentShaderModule = device.createShaderModule({ code: finalFragmentCode });

            renderPipeline = device.createRenderPipeline({
                layout: 'auto',
                vertex: {
                    module: vertexShaderModule,
                    entryPoint: 'vs_main',
                },
                fragment: {
                    module: fragmentShaderModule,
                    entryPoint: 'fs_main',
                    targets: [{
                        format: canvasFormat,
                        blend: {
                            color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                        },
                    }],
                },
                primitive: { topology: 'triangle-list' },
                multisample: { count: sampleCount },
            });

            bindGroup = device.createBindGroup({
                layout: renderPipeline.getBindGroupLayout(0),
                entries: [{ binding: 0, resource: { buffer: uniformBuffer } }],
            });

            self = {
                draw: (pass: GPURenderPassEncoder) => {
                    if (!renderPipeline || !bindGroup) return; // 再次检查
                    pass.setPipeline(renderPipeline);
                    pass.setBindGroup(0, bindGroup);
                    pass.draw(6); // 绘制覆盖全屏的两个三角形
                },
                layer: layer,
            };
            register.add(self);
            requestRender();

        } catch (e) {
            // 提供更详细的错误信息
            console.error("Failed to initialize pipeline for formula ID:", formula?.id, "Expression:", formula?.wgsl_expression);
            if (e instanceof GPUInternalError) {
                console.error("WGSL compilation failed:", e.message);
                const lines = functionBody.split('\n'); // functionBody 应该在此刻被定义
                const match = e.message.match(/<error>:\s*(\d+):(\d+)/);
                if (match) {
                    const lineNum = parseInt(match[1]);
                    const colNum = parseInt(match[2]);
                    if (lineNum > 0 && lineNum <= lines.length) {
                        console.error(`Error in generated WGSL (Line ${lineNum}, Col ${colNum}):`);
                        console.error(`Source: \`${lines[lineNum - 1].trim()}\``);
                        console.error(`           ${" ".repeat(colNum - 1)}^`);
                    }
                }
            } else {
                console.error("Other error during pipeline initialization:", e);
            }
            // 清理可能已创建的部分资源以防污染后续操作
            renderPipeline = null;
            bindGroup = null;
            uniformBuffer?.destroy();
            uniformBuffer = null;
        }
    }

    // 更新 Uniform Buffer
    $: if (uniformBuffer && self) {
        device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
        device.queue.writeBuffer(uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        device.queue.writeBuffer(uniformBuffer, 32, new Float32Array([color.r, color.g, color.b, color.a]));
    }

    onDestroy(() => {
        if (self) {
            register.delete(self);
        }
        uniformBuffer?.destroy();
        // 清理引用
        renderPipeline = null;
        bindGroup = null;
        uniformBuffer = null;
        self = null;
    });
</script>