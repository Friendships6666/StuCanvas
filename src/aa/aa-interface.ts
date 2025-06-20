// src/aa/aa-interface.ts

export interface IAARenderer {
    readonly sampleCount: number;

    initialize(
        device: GPUDevice,
        context: GPUCanvasContext,
        canvasFormat: GPUTextureFormat,
        canvasWidth: number,
        canvasHeight: number
    ): void;

    // ✅ 新增：要求所有 AA 渲染器都必须能响应尺寸变化
    resize(newWidth: number, newHeight: number): void;

    beginScenePass(encoder: GPUCommandEncoder, clearValue: GPUColor): GPURenderPassEncoder;

    execute(encoder: GPUCommandEncoder): void;

    destroy(): void;
}