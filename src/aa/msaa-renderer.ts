// src/aa/msaa-renderer.ts

import type { IAARenderer } from './aa-interface';

export class MSAARenderer implements IAARenderer {
    private device!: GPUDevice;
    private context!: GPUCanvasContext;
    private canvasFormat!: GPUTextureFormat;
    private msaaTexture: GPUTexture | undefined;

    public readonly sampleCount: 4 | 8;

    constructor(sampleCount: 4 | 8 = 4) {
        if (sampleCount !== 4 && sampleCount !== 8) {
            console.warn(`Invalid MSAA sampleCount ${sampleCount}. Defaulting to 4.`);
            this.sampleCount = 4;
        } else {
            this.sampleCount = sampleCount;
        }
    }

    public initialize(device: GPUDevice, context: GPUCanvasContext, canvasFormat: GPUTextureFormat, canvasWidth: number, canvasHeight: number): void {
        this.device = device;
        this.context = context;
        this.canvasFormat = canvasFormat;
        this.recreateTexture(canvasWidth, canvasHeight);
    }

    private recreateTexture(width: number, height: number): void {
        this.msaaTexture?.destroy();
        this.msaaTexture = this.device.createTexture({
            label: `MSAA ${this.sampleCount}x Render Texture`,
            size: [width, height],
            sampleCount: this.sampleCount,
            format: this.canvasFormat,
            usage: GPUTextureUsage.RENDER_ATTACHMENT,
        });
    }

    public resize(newWidth: number, newHeight: number): void {
        if (!this.device) return;
        this.recreateTexture(newWidth, newHeight);
    }

    public beginScenePass(encoder: GPUCommandEncoder, clearValue: GPUColor): GPURenderPassEncoder {
        if (!this.msaaTexture || !this.context) {
            throw new Error("MSAARenderer not initialized.");
        }
        return encoder.beginRenderPass({
            colorAttachments: [{
                view: this.msaaTexture.createView(),
                resolveTarget: this.context.getCurrentTexture().createView(),
                loadOp: 'clear',
                clearValue: clearValue,
                storeOp: 'discard', // 'discard' is fine for MSAA intermediate texture
            }],
        });
    }

    public execute(encoder: GPUCommandEncoder): void { /* No-op */ }

    public destroy(): void {
        this.msaaTexture?.destroy();
    }

    // ✅ *** 核心修复：实现接口要求的方法 ***
    /**
     * 返回内部 MSAA 纹理的视图，以便在其他渲染通道中使用。
     */
    public getTextureView(): GPUTextureView {
        if (!this.msaaTexture) {
            throw new Error("MSAARenderer not initialized or texture not created.");
        }
        return this.msaaTexture.createView();
    }
}