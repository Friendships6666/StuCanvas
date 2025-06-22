import { generateMaskShader } from './generateMaskShader';
import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';

export interface MaskGenerationResources {
    pipeline: GPUComputePipeline;
    texture: GPUTexture;
    bindGroup: GPUBindGroup;
    formulas: DrawableFormula[];
}

export function createValidationMaskTexture(device: GPUDevice, width: number, height: number): GPUTexture {
    return device.createTexture({
        label: 'Validation Mask Texture',
        size: [width, height],
        format: 'rgba8unorm',
        usage:
            GPUTextureUsage.STORAGE_BINDING |
            GPUTextureUsage.TEXTURE_BINDING |
            GPUTextureUsage.RENDER_ATTACHMENT,
    });
}

export async function initializeMaskResources(
    device: GPUDevice,
    uniformBuffer: GPUBuffer,
    formulas: DrawableFormula[],
    canvasWidth: number,
    canvasHeight: number
): Promise<MaskGenerationResources | null> {
    if (formulas.length === 0) return null;

    const validationTexture = createValidationMaskTexture(device, canvasWidth, canvasHeight);

    const shaderCode = generateMaskShader(formulas);
    if (!shaderCode) return null;

    const bindGroupLayout = device.createBindGroupLayout({
        label: 'Mask Generation Bind Group Layout',
        entries: [
            {
                binding: 0,
                visibility: GPUShaderStage.COMPUTE,
                buffer: {
                    type: 'uniform',
                    hasDynamicOffset: true,
                }
            },
            {
                binding: 1,
                visibility: GPUShaderStage.COMPUTE,
                storageTexture: {
                    access: 'write-only',
                    format: 'rgba8unorm'
                }
            }
        ]
    });

    const pipelineLayout = device.createPipelineLayout({
        bindGroupLayouts: [bindGroupLayout]
    });

    const module = device.createShaderModule({
        label: 'Mask Generation Compute Module',
        code: shaderCode,
    });

    const pipeline = await device.createComputePipelineAsync({
        label: 'Mask Generation Pipeline',
        layout: pipelineLayout,
        compute: {
            module: module,
            entryPoint: 'main',
        },
    });

    const bindGroup = device.createBindGroup({
        label: 'Mask Generation Bind Group',
        layout: bindGroupLayout,
        entries: [
            {
                binding: 0,
                resource: { buffer: uniformBuffer, size: 256 },
            },
            {
                binding: 1,
                resource: validationTexture.createView(),
            },
        ],
    });

    return {
        pipeline: pipeline,
        texture: validationTexture,
        bindGroup: bindGroup,
        formulas: formulas,
    };
}