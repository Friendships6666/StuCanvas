// src/aa/fxaa-renderer.ts
import type { IAARenderer } from './aa-interface';

export class FXAARenderer implements IAARenderer {
    readonly sampleCount = 1;
    private device!: GPUDevice;
    private context!: GPUCanvasContext;
    private sceneTexture!: GPUTexture;
    private frameInfoBuffer!: GPUBuffer;
    private blitPipeline!: GPURenderPipeline;
    private blitBindGroupLayout!: GPUBindGroupLayout;
    private blitBindGroup!: GPUBindGroup;

    initialize(device: GPUDevice, context: GPUCanvasContext, canvasFormat: GPUTextureFormat, canvasWidth: number, canvasHeight: number) {
        this.device = device;
        this.context = context;

        this.sceneTexture = device.createTexture({
            label: 'FXAA RectangularCore Texture',
            size: [canvasWidth, canvasHeight],
            format: canvasFormat,
            usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT,
        });

        this.frameInfoBuffer = device.createBuffer({
            label: 'FXAA Frame Info Buffer',
            size: 2 * 4,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });
        device.queue.writeBuffer(this.frameInfoBuffer, 0, new Float32Array([1.0 / canvasWidth, 1.0 / canvasHeight]));

        const sampler = device.createSampler({ magFilter: 'linear', minFilter: 'linear' });

        const blitShaderModule = device.createShaderModule({
            label: 'FXAA Post-Process Shader',
            code: `
        @group(0) @binding(0) var u_sceneTexture: texture_2d<f32>;
        @group(0) @binding(1) var u_sampler: sampler;
        @group(0) @binding(2) var<uniform> u_rcpFrame: vec2f;

        const EDGE_THRESHOLD_MIN: f32 = 0.0312;
        const EDGE_THRESHOLD_MAX: f32 = 0.125;

        fn rgb2luma(c: vec3f) -> f32 { return dot(c, vec3f(0.299, 0.587, 0.114)); }

        @vertex
        fn vertexMain(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
            let pos = array(vec2f(-1.0, -1.0), vec2f(-1.0, 3.0), vec2f(3.0, -1.0));
            return vec4f(pos[i], 0.0, 1.0);
        }

        @fragment
        fn fragmentMain(@builtin(position) fragCoord: vec4f) -> @location(0) vec4f {
            let uv = fragCoord.xy * u_rcpFrame;

            // --- FIX: 提前进行所有必要的纹理采样，以满足统一控制流 ---
            let colorCenter = textureSample(u_sceneTexture, u_sampler, uv);
            let colorN = textureSample(u_sceneTexture, u_sampler, uv + vec2f(0.0, u_rcpFrame.y));
            let colorS = textureSample(u_sceneTexture, u_sampler, uv - vec2f(0.0, u_rcpFrame.y));
            let colorE = textureSample(u_sceneTexture, u_sampler, uv + vec2f(u_rcpFrame.x, 0.0));
            let colorW = textureSample(u_sceneTexture, u_sampler, uv - vec2f(u_rcpFrame.x, 0.0));
            let colorNW = textureSample(u_sceneTexture, u_sampler, uv + vec2f(-u_rcpFrame.x, u_rcpFrame.y));
            let colorNE = textureSample(u_sceneTexture, u_sampler, uv + vec2f(u_rcpFrame.x, u_rcpFrame.y));
            let colorSW = textureSample(u_sceneTexture, u_sampler, uv - vec2f(u_rcpFrame.x, u_rcpFrame.y));
            let colorSE = textureSample(u_sceneTexture, u_sampler, uv + vec2f(u_rcpFrame.x, -u_rcpFrame.y));

            // --- 现在，所有后续计算都只使用这些预先采样好的局部变量 ---
            let lumaCenter = rgb2luma(colorCenter.rgb);
            let lumaN = rgb2luma(colorN.rgb);
            let lumaS = rgb2luma(colorS.rgb);
            let lumaE = rgb2luma(colorE.rgb);
            let lumaW = rgb2luma(colorW.rgb);

            let lumaMin = min(lumaCenter, min(min(lumaN, lumaS), min(lumaE, lumaW)));
            let lumaMax = max(lumaCenter, max(max(lumaN, lumaS), max(lumaE, lumaW)));
            let lumaRange = lumaMax - lumaMin;

            if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
                return colorCenter; // 合法：返回一个局部变量
            }

            // 这里的计算现在也是合法的，因为它不涉及任何新的纹理采样
            let lumaNW = rgb2luma(colorNW.rgb);
            let lumaNE = rgb2luma(colorNE.rgb);
            let lumaSW = rgb2luma(colorSW.rgb);
            let lumaSE = rgb2luma(colorSE.rgb);

            let edgeHorizontal = abs((lumaNW + lumaNE) - (lumaSW + lumaSE));
            let edgeVertical = abs((lumaNW + lumaSW) - (lumaNE + lumaSE));
            let isHorizontal = edgeHorizontal >= edgeVertical;

            // 为了演示，我们用一个简化的混合来代替完整的FXAA搜索逻辑
            let mixedColor = (colorN + colorS + colorE + colorW) / 4.0;
            return mix(colorCenter, mixedColor, 0.75);
        }
      `,
        });

        this.blitBindGroupLayout = device.createBindGroupLayout({
            label: 'FXAA BindGroupLayout',
            entries: [
                { binding: 0, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
                { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
                { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
            ],
        });

        this.blitPipeline = device.createRenderPipeline({
            label: 'FXAA Pipeline',
            layout: device.createPipelineLayout({ bindGroupLayouts: [this.blitBindGroupLayout] }),
            vertex: { module: blitShaderModule, entryPoint: 'vertexMain' },
            fragment: { module: blitShaderModule, entryPoint: 'fragmentMain', targets: [{ format: canvasFormat }] },
        });

        this.blitBindGroup = device.createBindGroup({
            label: 'FXAA BindGroup',
            layout: this.blitBindGroupLayout,
            entries: [
                { binding: 0, resource: this.sceneTexture.createView() },
                { binding: 1, resource: sampler },
                { binding: 2, resource: { buffer: this.frameInfoBuffer } },
            ],
        });
    }

    beginScenePass(encoder: GPUCommandEncoder, clearValue: GPUColor): GPURenderPassEncoder {
        if (!this.sceneTexture) throw new Error("FXAARenderer not initialized.");
        return encoder.beginRenderPass({
            colorAttachments: [{
                view: this.sceneTexture.createView(),
                loadOp: 'clear',
                // --- 使用传入的参数，而不是硬编码的颜色 ---
                clearValue: clearValue,
                storeOp: 'store',
            }],
        });
    }

    execute(encoder: GPUCommandEncoder) {
        if (!this.blitPipeline || !this.blitBindGroup) return;
        const pass = encoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.getCurrentTexture().createView(),
                loadOp: 'load',
                storeOp: 'store',
            }],
        });
        pass.setPipeline(this.blitPipeline);
        pass.setBindGroup(0, this.blitBindGroup);
        pass.draw(3);
        pass.end();
    }

    destroy() {
        this.sceneTexture?.destroy();
        this.frameInfoBuffer?.destroy();
    }
}