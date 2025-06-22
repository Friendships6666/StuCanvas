import { generateSdfShader } from './generateSdfShader';
import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';
import sdfVertexShader from './sdfVertex.wgsl?raw';
import sdfFragmentTemplate from './sdfFragment.wgsl?raw';

export interface SdfRenderResources {
    pipeline: GPURenderPipeline;
    bindGroup: GPUBindGroup;
    functionDataBuffer: GPUBuffer;
}

export async function initializeSdfResources(
    device: GPUDevice,
    uniformBuffer: GPUBuffer,
    validationMaskTexture: GPUTexture,
    formulas: DrawableFormula[],
    canvasFormat: GPUTextureFormat,
    sampleCount: number
): Promise<SdfRenderResources | null> {
    if (formulas.length === 0) return null;

    const functionData = new Float32Array(formulas.flatMap(f => [f.color.r, f.color.g, f.color.b, f.color.a]));
    const functionDataBuffer = device.createBuffer({
        label: 'Function Data Storage Buffer',
        size: Math.max(functionData.byteLength, 16),
        usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    device.queue.writeBuffer(functionDataBuffer, 0, functionData);

    const [definitions, evaluations] = generateSdfShader(formulas);
    const finalFragmentCode = sdfFragmentTemplate
        .replace('/*__WGSL_FUNCTION_DEFINITIONS__*/', definitions)
        .replace('/*__WGSL_FUNCTION_EVALUATIONS__*/', evaluations);

    const bindGroupLayout = device.createBindGroupLayout({
        label: 'SDF Render Bind Group Layout',
        entries: [
            {
                binding: 0,
                visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                buffer: { type: 'uniform', hasDynamicOffset: true }
            },
            {
                binding: 1,
                visibility: GPUShaderStage.FRAGMENT,
                buffer: { type: 'read-only-storage' }
            },
            {
                binding: 2,
                visibility: GPUShaderStage.FRAGMENT,
                texture: { sampleType: 'unfilterable-float' }
            },
        ]
    });

    const pipelineLayout = device.createPipelineLayout({
        bindGroupLayouts: [bindGroupLayout]
    });

    const vertexModule = device.createShaderModule({ code: sdfVertexShader });
    const fragmentModule = device.createShaderModule({ code: finalFragmentCode });

    const pipeline = await device.createRenderPipelineAsync({
        label: 'SDF Validation Render Pipeline',
        layout: pipelineLayout,
        vertex: {
            module: vertexModule,
            entryPoint: 'vs_main',
        },
        fragment: {
            module: fragmentModule,
            entryPoint: 'fs_main',
            targets: [{
                format: canvasFormat,
                blend: {
                    color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
                    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
                },
            }],
        },
        primitive: { topology: 'triangle-list' },
        multisample: { count: sampleCount },
    });

    const bindGroup = device.createBindGroup({
        label: 'SDF Render Bind Group',
        layout: bindGroupLayout,
        entries: [
            { binding: 0, resource: { buffer: uniformBuffer, size: 256 } },
            { binding: 1, resource: { buffer: functionDataBuffer } },
            { binding: 2, resource: validationMaskTexture.createView() },
        ],
    });

    return {
        pipeline: pipeline,
        bindGroup: bindGroup,
        functionDataBuffer: functionDataBuffer,
    };
}