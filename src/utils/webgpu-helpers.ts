// src/utils/webgpu-helpers.ts

/**
 * 定义初始化后返回的对象类型，方便类型提示
 */
export interface WebGPUInit {
    device: GPUDevice;
    context: GPUCanvasContext;
    canvasFormat: GPUTextureFormat;
}

/**
 * 初始化WebGPU，获取核心的device, context和canvasFormat。
 * 这是一个可在任何WebGPU组件中复用的异步函数。
 * @param canvas - 需要在其上进行绘制的HTMLCanvasElement
 * @returns 一个包含device, context, canvasFormat的对象，或在失败时返回null
 */
export async function initializeWebGPU(canvas: HTMLCanvasElement): Promise<WebGPUInit | null> {
    try {
        if (!navigator.gpu) {
            console.error("WebGPU not supported on this browser.");
            return null;
        }

        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            console.error("No appropriate GPUAdapter found.");
            return null;
        }

        const device = await adapter.requestDevice();
        const context = canvas.getContext('webgpu');
        if (!context) {
            console.error("Could not get WebGPU context from canvas.");
            return null;
        }

        const canvasFormat = navigator.gpu.getPreferredCanvasFormat();

        context.configure({
            device: device,
            format: canvasFormat,
        });

        return { device, context, canvasFormat };

    } catch (e) {
        console.error("Error initializing WebGPU:", e);
        return null;
    }
}