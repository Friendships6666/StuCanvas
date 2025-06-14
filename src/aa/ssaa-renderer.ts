import type { IAARenderer } from './aa-interface';

export class SSAARenderer implements IAARenderer {
    // SSAA 的主场景渲染通道本身是非多重采样的，所以 sampleCount 是 1
    readonly sampleCount = 1;

    // 私有属性，用于存储核心对象
    private device!: GPUDevice;
    private context!: GPUCanvasContext;
    private canvasFormat!: GPUTextureFormat;
    private supersampleTexture!: GPUTexture;
    private blitPipeline!: GPURenderPipeline;
    private blitBindGroup!: GPUBindGroup;
    private sampleScale: number;

    /**
     * @param sampleScale - 采样倍数，例如 1.5 或 2.0。不建议过高。
     */
    constructor(sampleScale: number = 1.5) {
        this.sampleScale = sampleScale;
    }

    /**
     * 初始化渲染器，并首次创建所需资源。
     */
    public initialize(device: GPUDevice, context: GPUCanvasContext, canvasFormat: GPUTextureFormat, canvasWidth: number, canvasHeight: number): void {
        this.device = device;
        this.context = context;
        this.canvasFormat = canvasFormat;
        this.recreateResources(canvasWidth, canvasHeight);
    }

    /**
     * 核心的资源创建/重建函数。
     * 可在初始化和窗口大小调整时重复调用。
     * @param width - 画布的当前宽度
     * @param height - 画布的当前高度
     */
    private recreateResources(width: number, height: number): void {
        // 在创建新纹理前，务必销毁旧的，防止内存泄漏
        this.supersampleTexture?.destroy();

        let supersampleWidth = Math.floor(width * this.sampleScale);
        let supersampleHeight = Math.floor(height * this.sampleScale);

        // ✅ 关键修复：检查并遵守硬件的纹理尺寸限制
        const { maxTextureDimension2D } = this.device.limits;
        if (supersampleWidth > maxTextureDimension2D || supersampleHeight > maxTextureDimension2D) {
            console.warn(
                `Requested SSAA texture size (${supersampleWidth}x${supersampleHeight}) exceeds device limit (${maxTextureDimension2D}). Clamping size.`
            );
            // 将超出的尺寸约束在限制之内
            supersampleWidth = Math.min(supersampleWidth, maxTextureDimension2D);
            supersampleHeight = Math.min(supersampleHeight, maxTextureDimension2D);
        }

        // 创建一个用于离屏渲染的、尺寸更大的纹理
        this.supersampleTexture = this.device.createTexture({
            label: `SSAA ${this.sampleScale}x Render Texture`,
            size: [supersampleWidth, supersampleHeight],
            format: this.canvasFormat,
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT,
        });

        // --- 创建用于“缩小”后期处理的管线和绑定 ---
        const sampler = this.device.createSampler({ magFilter: 'linear', minFilter: 'linear' });

        const blitShaderModule = this.device.createShaderModule({
            label: 'SSAA Blit Shader',
            code: `
                // ✅ 使用 Uniform Buffer 动态传递分辨率
                struct Uniforms {
                    resolution: vec2f,
                }
                @group(0) @binding(0) var u_sampler: sampler;
                @group(0) @binding(1) var u_texture: texture_2d<f32>;
                @group(0) @binding(2) var<uniform> u_uniforms: Uniforms;

                @vertex 
                fn vertexMain(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
                    // 创建一个能覆盖全屏的三角形
                    let pos = array(vec2f(-1, -1), vec2f(-1, 3), vec2f(3, -1));
                    return vec4f(pos[i], 0.0, 1.0);
                }

                @fragment 
                fn fragmentMain(@builtin(position) fragCoord: vec4f) -> @location(0) vec4f {
                    let uv = fragCoord.xy / u_uniforms.resolution;
                    return textureSample(u_texture, u_sampler, uv);
                }
            `,
        });

        this.blitPipeline = this.device.createRenderPipeline({
            label: 'SSAA Blit Pipeline',
            layout: 'auto', // 使用 'auto' 布局，让 WebGPU 自动推断绑定组布局
            vertex: { module: blitShaderModule, entryPoint: 'vertexMain' },
            fragment: { module: blitShaderModule, entryPoint: 'fragmentMain', targets: [{ format: this.canvasFormat }] },
        });

        // 创建并填充存放分辨率的 Uniform Buffer
        const uniformBuffer = this.device.createBuffer({
            size: 8, // vec2f 需要 2 * 4 = 8 字节
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        this.device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([width, height]));

        this.blitBindGroup = this.device.createBindGroup({
            label: 'SSAA Blit BindGroup',
            layout: this.blitPipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: sampler },
                { binding: 1, resource: this.supersampleTexture.createView() },
                { binding: 2, resource: { buffer: uniformBuffer } },
            ],
        });
    }

    /**
     * 在画布尺寸改变时，由外部调用此方法来重建资源。
     * @param newWidth - 新的画布宽度
     * @param newHeight - 新的画布高度
     */
    public resize(newWidth: number, newHeight: number): void {
        if (!this.device) return;
        this.recreateResources(newWidth, newHeight);
    }

    /**
     * 开始场景的主渲染通道，将所有物体绘制到超大的离屏纹理上。
     */
    public beginScenePass(encoder: GPUCommandEncoder, clearValue: GPUColor): GPURenderPassEncoder {
        if (!this.supersampleTexture) {
            throw new Error("SSAARenderer resources are not available. Was initialization successful?");
        }
        return encoder.beginRenderPass({
            colorAttachments: [{
                view: this.supersampleTexture.createView(),
                loadOp: 'clear',
                clearValue: clearValue,
                storeOp: 'store',
            }],
        });
    }

    /**
     * 执行后期处理通道，将超采样纹理的内容，通过“缩小”管线绘制到最终的屏幕画布上。
     */
    public execute(encoder: GPUCommandEncoder): void {
        if (!this.blitPipeline || !this.blitBindGroup) return;

        const pass = encoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.getCurrentTexture().createView(),
                loadOp: 'clear', // 在绘制全屏三角形前，最好还是清空一下
                clearValue: { r: 0, g: 0, b: 0, a: 1 }, // 可选的清空色
                storeOp: 'store',
            }],
        });
        pass.setPipeline(this.blitPipeline);
        pass.setBindGroup(0, this.blitBindGroup);
        pass.draw(3); // 绘制一个覆盖全屏的三角形
        pass.end();
    }

    /**
     * 销毁创建的 GPU 资源。
     */
    public destroy(): void {
        this.supersampleTexture?.destroy();
    }
}