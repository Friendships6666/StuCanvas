import type { IAARenderer } from './aa-interface';

export class MSAARenderer implements IAARenderer {
    private device!: GPUDevice; // 添加 device 以便在 resize 中使用
    private context!: GPUCanvasContext;
    private canvasFormat!: GPUTextureFormat; // 添加 canvasFormat
    private msaaTexture: GPUTexture | undefined;

    public readonly sampleCount: 4 | 8;

    /**
     * @param sampleCount - 采样数，只能是 4 或 8。
     */
    constructor(sampleCount: 4 | 8 = 4) {
        // ✅ 增加一个检查，防止传入无效的采样数
        if (sampleCount !== 4 && sampleCount !== 8) {
            console.warn(`Invalid MSAA sampleCount ${sampleCount}. Defaulting to 4.`);
            this.sampleCount = 4;
        } else {
            this.sampleCount = sampleCount;
        }
    }

    /**
     * 初始化 MSAA 渲染器。
     */
    public initialize(device: GPUDevice, context: GPUCanvasContext, canvasFormat: GPUTextureFormat, canvasWidth: number, canvasHeight: number): void {
        this.device = device;
        this.context = context;
        this.canvasFormat = canvasFormat;
        this.recreateTexture(canvasWidth, canvasHeight);
    }

    /**
     * ✅ 新增：一个可重复调用的纹理创建函数
     * @param width - 新的画布宽度
     * @param height - 新的画布高度
     */
    private recreateTexture(width: number, height: number): void {
        // 在创建新纹理前，销毁旧的
        this.msaaTexture?.destroy();

        this.msaaTexture = this.device.createTexture({
            label: `MSAA ${this.sampleCount}x Render Texture`,
            size: [width, height],
            sampleCount: this.sampleCount,
            format: this.canvasFormat,
            usage: GPUTextureUsage.RENDER_ATTACHMENT,
        });
    }

    /**
     * ✅ 新增：公开的 resize 方法，以符合 IAARenderer 的扩展契约
     * @param newWidth - 新的画布宽度
     ** @param newHeight - 新的画布高度
     */
    public resize(newWidth: number, newHeight: number): void {
        if (!this.device) return;
        this.recreateTexture(newWidth, newHeight);
    }

    /**
     * 开始一个场景渲染通道。
     */
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
                storeOp: 'discard',
            }],
        });
    }

    /**
     * 对于 MSAA，此方法无操作。
     */
    public execute(encoder: GPUCommandEncoder): void { /* No-op */ }

    /**
     * 销毁创建的 GPU 资源。
     */
    public destroy(): void {
        this.msaaTexture?.destroy();
    }
}