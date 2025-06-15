<script lang="ts">
    import { onDestroy, onMount } from 'svelte';
    import type { View } from '../stores/camera';
    import type { Renderable } from '../coordinate/rectangular/interaction/renderer';
    import vertexShaderCode from './function.implicit.vertex.wgsl?raw';
    import fragmentShaderTemplate from './function.batch.fragment.wgsl?raw';
    import { translateJsExpressionToWgsl } from './wgsl-translator';

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: View;
    export let aspect: number;
    export let formulas: { id: string; wgsl_expression: string; color: {r:number, g:number, b:number, a:number} }[];
    export let requestRender: () => void;
    export let layer: number = 3;

    // --- GPU 资源 ---
    let renderPipeline: GPURenderPipeline | null = null;
    let uniformBuffer: GPUBuffer | null = null;
    let functionDataBuffer: GPUBuffer | null = null;
    let bindGroup: GPUBindGroup | null = null;
    let self: Renderable | null = null;

    $: if (device && formulas) {
        initializeBatchPipeline();
    }

    // ✅ *** FINAL, CORRECTED FIX: The alpha blending formula had a persistent typo. ***
    function generateFragmentShader(formulas: any[]): [string, string] {
        if (formulas.length === 0) return ["", ""];

        const definitions = formulas.map((f, i) => {
            const wgslExpr = translateJsExpressionToWgsl(f.wgsl_expression);
            return `fn eval_F_${i}(x: f32, y: f32) -> f32 { return ${wgslExpr}; }`;
        }).join('\n');

        const evaluations = formulas.map((_, i) => {
            return `
    // --- Function ${i} ---
    {
        let h_x = dpdx(x);
        let h_y = dpdy(y);

        let F_x_plus = eval_F_${i}(x + h_x, y);
        let F_x_minus = eval_F_${i}(x - h_x, y);
        let F_y_plus = eval_F_${i}(x, y + h_y);
        let F_y_minus = eval_F_${i}(x, y - h_y);

        var df_dx = 0.0;
        if (h_x != 0.0) {
            df_dx = (F_x_plus - F_x_minus) / (2.0 * h_x);
        }

        var df_dy = 0.0;
        if (h_y != 0.0) {
            df_dy = (F_y_plus - F_y_minus) / (2.0 * h_y);
        }

        let grad_len = length(vec2(df_dx, df_dy));
        let F_center = eval_F_${i}(x, y);

        let norm_dist = abs(F_center) / max(grad_len, 0.0001);

        let alpha = smoothstep(target_world_width, 0.0, norm_dist);

        if (alpha > 0.0) {
            let func_color = functions.data[${i}].color;
            let effective_alpha = alpha * func_color.a;

            // Correct "A-over-B" alpha blending for pre-multiplied alpha.
            // This is the robust and correct implementation.
            let blended_rgb = func_color.rgb * effective_alpha + final_color.rgb * (1.0 - effective_alpha);
            let blended_a = effective_alpha + final_color.a * (1.0 - effective_alpha);

            final_color = vec4<f32>(blended_rgb, blended_a);
        }
    }
    `;
        }).join('');

        return [definitions, evaluations];
    }


    async function initializeBatchPipeline() {
        if (self) {
            register.delete(self);
            self = null;
        }
        uniformBuffer?.destroy();
        functionDataBuffer?.destroy();

        if (formulas.length === 0) {
            requestRender();
            return;
        }

        try {
            uniformBuffer = device.createBuffer({
                label: `Batch Function Uniforms`,
                size: 32,
                usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
            });

            const functionData = new Float32Array(formulas.flatMap(f => [f.color.r, f.color.g, f.color.b, f.color.a]));
            functionDataBuffer = device.createBuffer({
                label: `Batch Function Data Storage`,
                size: Math.max(functionData.byteLength, 16),
                usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
            });
            device.queue.writeBuffer(functionDataBuffer, 0, functionData);

            const [definitions, evaluations] = generateFragmentShader(formulas);
            if (!definitions) {
                if (self) register.delete(self);
                self = null;
                requestRender();
                return;
            }

            const finalFragmentCode = fragmentShaderTemplate
                .replace('/*__WGSL_FUNCTION_DEFINITIONS__*/', definitions)
                .replace('/*__WGSL_FUNCTION_EVALUATIONS__*/', evaluations);

            const fragmentShaderModule = device.createShaderModule({ code: finalFragmentCode });
            const vertexShaderModule = device.createShaderModule({ code: vertexShaderCode });

            renderPipeline = await device.createRenderPipelineAsync({
                label: 'Batch Function Pipeline',
                layout: 'auto',
                vertex: { module: vertexShaderModule, entryPoint: 'vs_main' },
                fragment: {
                    module: fragmentShaderModule,
                    entryPoint: 'fs_main',
                    targets: [{
                        format: canvasFormat,
                        blend: {
                            color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                        },
                    }],
                },
                primitive: { topology: 'triangle-list' },
                multisample: { count: sampleCount },
            });

            bindGroup = device.createBindGroup({
                label: 'Batch Function Bind Group',
                layout: renderPipeline.getBindGroupLayout(0),
                entries: [
                    { binding: 0, resource: { buffer: uniformBuffer } },
                    { binding: 1, resource: { buffer: functionDataBuffer } },
                ],
            });

            self = {
                draw: (pass: GPURenderPassEncoder) => {
                    if (!renderPipeline || !bindGroup || formulas.length === 0) return;
                    pass.setPipeline(renderPipeline);
                    pass.setBindGroup(0, bindGroup);
                    pass.draw(6);
                },
                layer: layer,
            };
            register.add(self);
            requestRender();

        } catch (e) {
            console.error("Failed to initialize batch function pipeline:", e);
            renderPipeline = null;
            bindGroup = null;
            uniformBuffer?.destroy();
            functionDataBuffer?.destroy();
        }
    }

    $: if (uniformBuffer && self) {
        device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
        device.queue.writeBuffer(uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        requestRender();
    }

    onDestroy(() => {
        if (self) register.delete(self);
        uniformBuffer?.destroy();
        functionDataBuffer?.destroy();
    });

</script>