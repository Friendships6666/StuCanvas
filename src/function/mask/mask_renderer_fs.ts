import type { Renderable } from '../../renderCore/renderer';
import type { View } from '../../stores/camera';
import vertexShaderCode from './mask_vertex_fs.wgsl?raw';
import fragmentShaderCode from './mask_fragment_fs.wgsl?raw';

// 这个新架构更简单，不需要那么多资源
export interface MaskRendererFsResources {
    renderPipeline: GPURenderPipeline;
    uniformBuffer: GPUBuffer;
    bindGroup: GPUBindGroup;
    renderable: Renderable;
}

export async function initializeMaskRendererFs(
    device: GPUDevice,
    canvasFormat: GPUTextureFormat,
    sampleCount: number
): Promise<MaskRendererFsResources> {

    const uniformBuffer = device.createBuffer({
        label: 'Mask FS Uniform Buffer',
        size: 32,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    const vertexModule = device.createShaderModule({ code: vertexShaderCode });
    const fragmentModule = device.createShaderModule({ code: fragmentShaderCode });

    const renderPipeline = await device.createRenderPipelineAsync({
        label: 'Mask FS Render Pipeline',
        layout: 'auto',
        vertex: { module: vertexModule, entryPoint: 'vs_main' },
        fragment: {
            module: fragmentModule,
            entryPoint: 'fs_main',
            targets: [{
                format: canvasFormat,
                // 我们需要混合，因为背景不是黑的
                blend: {
                    color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                }
            }],
        },
        primitive: { topology: 'triangle-list' },
        multisample: { count: sampleCount },
    });

    const bindGroup = device.createBindGroup({
        label: 'Mask FS Bind Group',
        layout: renderPipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: { buffer: uniformBuffer } }],
    });

    const renderable: Renderable = {
        update: (device: GPUDevice, view: View, aspect: number, canvas: HTMLCanvasElement) => {
            device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
            device.queue.writeBuffer(uniformBuffer, 16, new Float32Array([canvas.width, canvas.height]));
        },
        // 这个新方法没有 compute 或 preCompute 阶段
        draw: (pass: GPURenderPassEncoder) => {
            pass.setPipeline(renderPipeline);
            pass.setBindGroup(0, bindGroup);
            pass.draw(3); // 绘制一个覆盖全屏的三角形
        },
        layer: 4,
    };

    return { renderPipeline, uniformBuffer, bindGroup, renderable };
}