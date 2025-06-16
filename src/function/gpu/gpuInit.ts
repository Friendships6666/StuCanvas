// src/function/gpuInit.ts

import type { Renderable } from '../../interaction/input/renderer';
import { generateFragmentShader } from '../render/wgsl-generator';
import vertexShaderCode from './functionVertex.wgsl?raw';
import fragmentShaderTemplate from './functionFragment.wgsl?raw';

export interface BatchRendererGpuResources {
    renderPipeline: GPURenderPipeline;
    uniformBuffer: GPUBuffer;
    functionDataBuffer: GPUBuffer;
    bindGroup: GPUBindGroup;
    renderable: Renderable;
}

/**
 * 初始化WebGPU相关资源
 */
export async function initializeGpuResources(
    device: GPUDevice,
    canvasFormat: GPUTextureFormat,
    sampleCount: number,
    formulas: any[]
): Promise<BatchRendererGpuResources | null> {
    if (formulas.length === 0) return null;

    try {
        const uniformBuffer = device.createBuffer({
            label: `Batch Function Uniforms`,
            size: 32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });

        const functionData = new Float32Array(formulas.flatMap(f => [f.color.r, f.color.g, f.color.b, f.color.a]));
        const functionDataBuffer = device.createBuffer({
            label: `Batch Function Data Storage`,
            size: Math.max(functionData.byteLength, 16),
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(functionDataBuffer, 0, functionData);

        const [definitions, evaluations] = generateFragmentShader(formulas);
        if (!definitions) return null;

        const finalFragmentCode = fragmentShaderTemplate
            .replace('/*__WGSL_FUNCTION_DEFINITIONS__*/', definitions)
            .replace('/*__WGSL_FUNCTION_EVALUATIONS__*/', evaluations);

        const fragmentShaderModule = device.createShaderModule({ code: finalFragmentCode });
        const vertexShaderModule = device.createShaderModule({ code: vertexShaderCode });

        const renderPipeline = await device.createRenderPipelineAsync({
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

        const bindGroup = device.createBindGroup({
            label: 'Batch Function Bind Group',
            layout: renderPipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: uniformBuffer } },
                { binding: 1, resource: { buffer: functionDataBuffer } },
            ],
        });

        const renderable: Renderable = {
            draw: (pass: GPURenderPassEncoder) => {
                if (!renderPipeline || !bindGroup || formulas.length === 0) return;
                pass.setPipeline(renderPipeline);
                pass.setBindGroup(0, bindGroup);
                pass.draw(6);
            },
            layer: 3, // 假设 layer 是固定的，或者可以作为参数传入
        };

        return { renderPipeline, uniformBuffer, functionDataBuffer, bindGroup, renderable };

    } catch (e) {
        console.error("Failed to initialize batch function pipeline:", e);
        return null;
    }
}