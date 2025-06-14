// src/aa/no-aa-renderer.ts
import type { IAARenderer } from './aa-interface';

export class NoAARenderer implements IAARenderer {
    private context: GPUCanvasContext | undefined;
    readonly sampleCount = 1;

    initialize(device: GPUDevice, context: GPUCanvasContext) {
        this.context = context;
    }

    // --- CHANGE: 修改 beginScenePass 以接受 clearValue ---
    beginScenePass(encoder: GPUCommandEncoder, clearValue: GPUColor): GPURenderPassEncoder {
        if (!this.context) throw new Error("NoAARenderer not initialized.");

        return encoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.getCurrentTexture().createView(),
                loadOp: 'clear',
                // --- 使用传入的参数，而不是硬编码的白色 ---
                clearValue: clearValue,
                storeOp: 'store',
            }],
        });
    }

    execute(encoder: GPUCommandEncoder) { /* No-op */ }
    destroy() { /* 无操作 */ }
}