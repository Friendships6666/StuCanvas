// src/lib/webgpu/device.ts

import type { GpuContext } from './types';

export async function initializeWebGpuContext(canvas: HTMLCanvasElement): Promise<GpuContext | null> {
    try {
        if (!navigator.gpu) {
            throw new Error('WebGPU not supported on this browser.');
        }
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            throw new Error('No appropriate GPUAdapter found.');
        }

        const device = await adapter.requestDevice({
            requiredLimits: {
                maxBufferSize: adapter.limits.maxBufferSize,
                maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
            }
        });

        const context = canvas.getContext('webgpu')!;
        const presentationFormat = navigator.gpu.getPreferredCanvasFormat();
        context.configure({ device, format: presentationFormat });

        return { device, context, presentationFormat };
    } catch (err) {
        console.error(err);
        return null;
    }
}