<!-- src/geometry/FunctionGraph.svelte (Final Correct Version) -->
<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { View } from '../stores/camera';
    import type { Renderable } from '../coordinate/rectangular/interaction/renderer';
    import vertexShaderCode from './function.implicit.vertex.wgsl?raw';
    import fragmentShaderTemplate from './function.implicit.fragment.wgsl?raw';

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: View;
    export let aspect: number;
    // ✅ 简化 formula prop 类型，不再需要导函数
    export let formula: { id: string; expression: string; left_side: 'y' };
    export let requestRender: () => void;
    export let color = { r: 0.1, g: 0.4, b: 0.9, a: 1.0 };
    export let layer: number = 3;

    // --- GPU 资源 ---
    let renderPipeline: GPURenderPipeline | null = null;
    let uniformBuffer: GPUBuffer | null = null;
    let bindGroup: GPUBindGroup | null = null;
    let self: Renderable | null = null;

    function translateExpressionToWgsl(jsExpr: string): string {
        let wgslExpr = jsExpr.trim()
            .replace(/\bpi\b/g, '3.1415926535');

        wgslExpr = wgslExpr.replace(/([\w\d.]+|\([^)]+\))\s*\^\s*([\w\d.]+)/g, "pow($1, $2)");
        wgslExpr = wgslExpr.replace(/(?<![.\w])(\d+)(?![.\w])/g, '$1.0');

        return wgslExpr;
    }

    // --- 初始化 ---
    $: device && formula.expression && initializePipeline();

    function initializePipeline() {
        if (self) {
            register.delete(self);
            self = null;
        }
        uniformBuffer?.destroy();

        try {
            uniformBuffer = device.createBuffer({
                label: `Uniforms for ${formula.id}`,
                size: 64,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });

            const functionBody = `return ${translateExpressionToWgsl(formula.expression)};`;

            // ✅ 只需注入原函数
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
                    if (!renderPipeline || !bindGroup) return;
                    pass.setPipeline(renderPipeline);
                    pass.setBindGroup(0, bindGroup);
                    pass.draw(6);
                },
                layer: layer,
            };
            register.add(self);
            requestRender();

        } catch (e) {
            console.error("Failed to create implicit pipeline for formula:", formula.expression, e);
        }
    }

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
    });
</script>