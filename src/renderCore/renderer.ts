// src/renderCore/renderer.ts (请用下面的完整代码覆盖)

import type { IAARenderer } from '../aa/aa-interface';
import { MSAARenderer } from '../aa/msaa-renderer';
import { initializeWebGPU } from './webgpu-helpers';
import type { View } from '../stores/camera'; // 引入View类型

// ✅ 1. 修改 Renderable 接口，加入一个正式的 update 方法
export interface Renderable {
    update?: (device: GPUDevice, view: View, aspect: number, canvas: HTMLCanvasElement) => void;
    preCompute?: (encoder: GPUCommandEncoder) => void;
    compute?: (pass: GPUComputePassEncoder) => void;
    draw: (pass: GPURenderPassEncoder) => void;
    layer?: number;
}

export interface Renderer {
    device: GPUDevice;
    context: GPUCanvasContext;
    aaRenderer: IAARenderer;
    canvasFormat: GPUTextureFormat;
    // ✅ 2. 修改 render 函数签名，让它接收每一帧的最新状态
    render: (components: Renderable[], view: View, aspect: number) => void;
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

    // ✅ 3. 实现新的、包含 update 阶段的渲染循环
    const render = (components: Renderable[], view: View, aspect: number) => {
        const encoder = device.createCommandEncoder({ label: "Main Command Encoder" });
        const sortedComponents = components.sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));

        // --- 阶段零: 更新 (Update) ---
        // 在本帧所有GPU操作开始前，强制所有组件更新它们的GPU状态（如Uniform Buffer）
        for (const component of sortedComponents) {
            component.update?.(device, view, aspect, canvas);
        }

        // --- 阶段一: 预计算 (Pre-Compute) ---
        for (const component of sortedComponents) {
            component.preCompute?.(encoder);
        }

        // --- 阶段二: 计算 (Compute) ---
        const computePass = encoder.beginComputePass({ label: "Main Compute Pass" });
        for (const component of sortedComponents) {
            component.compute?.(computePass);
        }
        computePass.end();

        // --- 阶段三: 绘制 (Draw) ---
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