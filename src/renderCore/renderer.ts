// src/interaction/input/renderer.ts (最终正确版)
import type { IAARenderer } from '../aa/aa-interface';
import { MSAARenderer } from '../aa/msaa-renderer';
import { initializeWebGPU } from './webgpu-helpers';

// ✅ FIX: 明确定义 compute 方法接收一个 GPUComputePassEncoder
export interface Renderable {
    draw: (pass: GPURenderPassEncoder) => void;
    compute?: (pass: GPUComputePassEncoder) => void;
    layer?: number;
}

export interface Renderer {
    device: GPUDevice;
    context: GPUCanvasContext;
    aaRenderer: IAARenderer;
    canvasFormat: GPUTextureFormat;
    render: (components: Renderable[]) => void;
    destroy: () => void;
    resize: (width: number, height: number) => void;
}

export async function initializeRenderer(canvas: HTMLCanvasElement): Promise<Renderer | null> {
    const gpuInit = await initializeWebGPU(canvas);
    if (!gpuInit) return null;

    const { device, context } = gpuInit;
    const canvasFormat = navigator.gpu.getPreferredCanvasFormat();
    const backgroundColor = { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };

    const aaRenderer = new MSAARenderer(4);
    aaRenderer.initialize(device, context, canvasFormat, canvas.width, canvas.height);

    // ✅ FIX: 实现经典、正确的两通道渲染循环
    const render = (components: Renderable[]) => {
        const encoder = device.createCommandEncoder({ label: "Main Command Encoder" });
        const sortedComponents = components.sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));

        // --- 阶段一: 执行所有计算任务 ---
        const computePass = encoder.beginComputePass({ label: "Main Compute Pass" });
        for (const component of sortedComponents) {
            component.compute?.(computePass);
        }
        computePass.end();

        // --- 阶段二: 执行所有绘制任务 ---
        const renderPass = aaRenderer.beginScenePass(encoder, backgroundColor);
        for (const component of sortedComponents) {
            component.draw(renderPass);
        }
        renderPass.end();

        aaRenderer.execute(encoder);
        device.queue.submit([encoder.finish()]);
    };

    const resize = (width: number, height: number): void => {
        aaRenderer.resize(width, height);
    };

    const destroy = (): void => {
        aaRenderer?.destroy();
        device?.destroy();
    };

    return { device, context, aaRenderer, canvasFormat, render, destroy, resize };
}