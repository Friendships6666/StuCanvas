import type { Renderable } from '../../renderCore/renderer';
import type { View } from '../../stores/camera';
import computeShaderCode from './mask_compute.wgsl?raw';
import vertexShaderCode from './mask_vertex.wgsl?raw';
import fragmentShaderCode from './mask_fragment.wgsl?raw';

/**
 * 定义这个蒙版渲染器需要的所有GPU资源。
 */
export interface MaskGpuResources {
    maskTexture: GPUTexture;
    uniformBuffer: GPUBuffer;
    computePipeline: GPUComputePipeline;
    renderPipeline: GPURenderPipeline;
    computeBindGroup: GPUBindGroup;
    renderBindGroup: GPUBindGroup;
    renderable: Renderable;
}

/**
 * 初始化创建散点图蒙版所需的所有WebGPU资源。
 * @param device - 当前的 GPUDevice。
 * @param canvasFormat - 画布的目标纹理格式。
 * @param sampleCount - 多重采样的样本数。
 * @param canvasElement - HTML 画布元素，用于获取尺寸。
 * @returns 一个包含所有已创建资源的 Promise。
 */
export async function initializeMaskResources(
    device: GPUDevice,
    canvasFormat: GPUTextureFormat,
    sampleCount: number,
    canvasElement: HTMLCanvasElement
): Promise<MaskGpuResources> {

    // --- 1. 创建核心GPU资源 ---

    const maskTexture = device.createTexture({
        label: 'Scatter Mask Texture',
        size: [canvasElement.width, canvasElement.height],
        format: 'rgba8unorm',
        usage:
            GPUTextureUsage.STORAGE_BINDING |   // 允许作为存储目标被计算着色器写入
            GPUTextureUsage.TEXTURE_BINDING |   // 允许作为纹理被渲染着色器读取
            GPUTextureUsage.RENDER_ATTACHMENT,  // 允许作为渲染目标来清空它
    });

    const uniformBuffer = device.createBuffer({
        label: 'Mask Uniform Buffer',
        size: 32, // vec4<f32> (16) + vec2<f32> (8), 对齐后 32
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    const sampler = device.createSampler({
        magFilter: 'nearest',
        minFilter: 'nearest',
    });

    // --- 2. 创建着色器模块和管线 ---

    const computeModule = device.createShaderModule({ label: 'Mask Compute Module', code: computeShaderCode });
    const vertexModule = device.createShaderModule({ label: 'Mask Vertex Module', code: vertexShaderCode });
    const fragmentModule = device.createShaderModule({ label: 'Mask Fragment Module', code: fragmentShaderCode });

    const computePipeline = await device.createComputePipelineAsync({
        label: 'Mask Compute Pipeline',
        layout: 'auto',
        compute: { module: computeModule, entryPoint: 'main' },
    });

    const renderPipeline = await device.createRenderPipelineAsync({
        label: 'Mask Render Pipeline',
        layout: 'auto',
        vertex: { module: vertexModule, entryPoint: 'vs_main' },
        fragment: {
            module: fragmentModule,
            entryPoint: 'fs_main',
            targets: [{ format: canvasFormat }],
        },
        primitive: { topology: 'triangle-list' },
        multisample: { count: sampleCount },
    });

    // --- 3. 创建绑定组 ---

    const computeBindGroup = device.createBindGroup({
        label: 'Mask Compute Bind Group',
        layout: computePipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: maskTexture.createView() },
            { binding: 1, resource: { buffer: uniformBuffer } },
        ],
    });

    const renderBindGroup = device.createBindGroup({
        label: 'Mask Render Bind Group',
        layout: renderPipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: maskTexture.createView() },
            { binding: 1, resource: sampler },
        ],
    });

    // --- 4. 创建可渲染对象 (Renderable) ---
    // 这是我们暴露给主渲染循环的对象，包含了所有渲染指令。

    const renderable: Renderable = {
        /**
         * ✅ 核心修复：在每帧的开始，同步更新GPU的uniform数据。
         * 这个方法由主渲染循环在所有GPU操作前调用，彻底解决了竞态条件。
         */
        update: (device: GPUDevice, view: View, aspect: number, canvas: HTMLCanvasElement) => {
            // 将 canvas 数据写入到偏移量 0
            device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([canvas.width, canvas.height]));
            // 将 view 数据写入到偏移量 16
            device.queue.writeBuffer(uniformBuffer, 16, new Float32Array([view.x, view.y, view.zoom, aspect]));
        },

        /**
         * 在计算开始前，清空上一帧的蒙版纹理，保证每一帧都是从干净的状态开始。
         */
        preCompute: (encoder: GPUCommandEncoder) => {
            const pass = encoder.beginRenderPass({
                colorAttachments: [{
                    view: maskTexture.createView(),
                    loadOp: 'clear',
                    storeOp: 'store',
                    clearValue: { r: 0, g: 0, b: 0, a: 0 }
                }]
            });
            pass.end();
        },

        /**
         * 执行计算任务：运行计算着色器来填充蒙版纹理。
         */
        compute: (pass: GPUComputePassEncoder) => {
            pass.setPipeline(computePipeline);
            pass.setBindGroup(0, computeBindGroup);
            const workgroupSize = 8;
            const workgroupsX = Math.ceil(canvasElement.width / workgroupSize);
            const workgroupsY = Math.ceil(canvasElement.height / workgroupSize);
            pass.dispatchWorkgroups(workgroupsX, workgroupsY);
        },

        /**
         * 执行绘制任务：将填充好的蒙版纹理绘制到屏幕上。
         */
        draw: (pass: GPURenderPassEncoder) => {
            pass.setPipeline(renderPipeline);
            pass.setBindGroup(0, renderBindGroup);
            pass.draw(3); // 绘制一个覆盖全屏的三角形
        },
        layer: 4,
    };

    return {
        maskTexture,
        uniformBuffer,
        computePipeline,
        renderPipeline,
        computeBindGroup,
        renderBindGroup,
        renderable,
    };
}