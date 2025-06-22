import type { Renderable } from '../renderCore/renderer';
import type { DrawableFormula } from '../coordinate/rectangular/use/use-formulas';
import { initializeMaskResources, type MaskGenerationResources } from './mask/maskResources';
import { initializeSdfResources, type SdfRenderResources } from './sdf/sdfResources';

export interface UnifiedFunctionResources {
    maskResources: MaskGenerationResources;
    sdfResources: SdfRenderResources;
    renderable: Renderable;
    uniformBuffer: GPUBuffer;
}

export async function initializeUnifiedResources(
    device: GPUDevice,
    canvasFormat: GPUTextureFormat,
    sampleCount: number,
    formulas: DrawableFormula[],
    canvasElement: HTMLCanvasElement
): Promise<UnifiedFunctionResources | null> {

    const uniformBufferSize = 256;
    const totalUniformSize = uniformBufferSize * (1 + formulas.length);

    const uniformBuffer = device.createBuffer({
        label: 'Shared Uniform Buffer (Dynamic)',
        size: totalUniformSize,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    const maskRes = await initializeMaskResources(
        device,
        uniformBuffer,
        formulas,
        canvasElement.width,
        canvasElement.height
    );

    if (!maskRes) {
        uniformBuffer.destroy();
        return null;
    }

    const sdfRes = await initializeSdfResources(
        device,
        uniformBuffer,
        maskRes.texture,
        formulas,
        canvasFormat,
        sampleCount
    );

    if (!sdfRes) {
        uniformBuffer.destroy();
        maskRes.texture.destroy();
        return null;
    }

    const renderable: Renderable = {
        compute: (pass: GPUComputePassEncoder) => {
            pass.setPipeline(maskRes.pipeline);

            const numPointsPerFunction = 200000;
            const workgroups = Math.ceil(numPointsPerFunction / 256);

            for (let i = 0; i < maskRes.formulas.length; i++) {
                const dynamicOffset = (i + 1) * uniformBufferSize;
                pass.setBindGroup(0, maskRes.bindGroup, [dynamicOffset]);
                pass.dispatchWorkgroups(workgroups);
            }
        },
        draw: (pass: GPURenderPassEncoder) => {
            pass.setPipeline(sdfRes.pipeline);
            pass.setBindGroup(0, sdfRes.bindGroup, [0]);
            pass.draw(6);
        },
        layer: 3,
        texturesToClear: [maskRes.texture], // 将验证蒙版暴露出去
    };

    return {
        maskResources: maskRes,
        sdfResources: sdfRes,
        renderable: renderable,
        uniformBuffer: uniformBuffer,
    };
}