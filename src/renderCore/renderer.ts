/*src/renderCore/renderer.ts*/
import type { IAARenderer } from '../aa/aa-interface';
import { MSAARenderer } from '../aa/msaa-renderer';
import { initializeWebGPU } from './webgpu-helpers';

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
    readDebugValueAt: (x: number, y: number) => Promise<number>;
}

export async function initializeRenderer(canvas: HTMLCanvasElement): Promise<Renderer | null> {
    const gpuInit = await initializeWebGPU(canvas);
    if (!gpuInit) return null;

    const { device, context } = gpuInit;
    const canvasFormat = navigator.gpu.getPreferredCanvasFormat();
    const backgroundColor = { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };

    const aaRenderer = new MSAARenderer(4);
    aaRenderer.initialize(device, context, canvasFormat, canvas.width, canvas.height);

    // --- 调试资源 ---
    let msaaDebugTexture = device.createTexture({
        label: 'MSAA Debug Value Texture',
        size: [canvas.width, canvas.height],
        sampleCount: aaRenderer.sampleCount,
        format: 'r32float',
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
    });
    let msaaDebugTextureView = msaaDebugTexture.createView();

    let resolvedDebugTexture = device.createTexture({
        label: 'Resolved Debug Value Texture (for Readback)',
        size: [canvas.width, canvas.height],
        sampleCount: 1,
        format: 'r32float',
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    let resolvedDebugTextureView = resolvedDebugTexture.createView();

    const readbackBuffer = device.createBuffer({
        label: 'GPU Readback Buffer',
        size: 4,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });

    const manualResolveShader = device.createShaderModule({
        label: 'Manual Resolve Shader',
        code: `
            @group(0) @binding(0) var msaaTex: texture_multisampled_2d<f32>;

            @vertex
            fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
                var pos = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
                return vec4f(pos[idx], 0.0, 1.0);
            }

            @fragment
            fn fs_main(@builtin(position) frag_coord: vec4f) -> @location(0) f32 {
                var total: f32 = 0.0;
                let pixel_coords = vec2i(floor(frag_coord.xy));
                for (var i: i32 = 0; i < 4; i = i + 1) {
                    total = total + textureLoad(msaaTex, pixel_coords, i).r;
                }
                return total / 4.0;
            }
        `,
    });

    const manualResolvePipeline = device.createRenderPipeline({
        label: 'Manual Resolve Pipeline',
        layout: 'auto',
        vertex: { module: manualResolveShader, entryPoint: 'vs_main' },
        fragment: {
            module: manualResolveShader,
            entryPoint: 'fs_main',
            targets: [{ format: 'r32float' }],
        },
    });

    let manualResolveBindGroup = device.createBindGroup({
        label: 'Manual Resolve BindGroup',
        layout: manualResolvePipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: msaaDebugTextureView }],
    });

    const render = (components: Renderable[]) => {
        const encoder = device.createCommandEncoder({ label: "Main Command Encoder" });
        const sortedComponents = components.sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));

        const computePass = encoder.beginComputePass({ label: "Main Compute Pass" });
        for (const component of sortedComponents) { component.compute?.(computePass); }
        computePass.end();

        const mainRenderPass = encoder.beginRenderPass({
            colorAttachments: [
                {
                    view: aaRenderer.getTextureView(),
                    resolveTarget: context.getCurrentTexture().createView(),
                    loadOp: 'clear', storeOp: 'store', clearValue: backgroundColor,
                },
                {
                    view: msaaDebugTextureView,
                    loadOp: 'clear', storeOp: 'store', clearValue: { r: -1.0, g: 0.0, b: 0.0, a: 0.0 },
                }
            ]
        });
        for (const component of sortedComponents) { component.draw(mainRenderPass); }
        mainRenderPass.end();

        const resolvePass = encoder.beginRenderPass({
            colorAttachments: [{
                view: resolvedDebugTextureView,
                loadOp: 'clear', storeOp: 'store', clearValue: { r: 0.0, g: 0.0, b: 0.0, a: 0.0 },
            }]
        });
        resolvePass.setPipeline(manualResolvePipeline);
        resolvePass.setBindGroup(0, manualResolveBindGroup);
        resolvePass.draw(3);
        resolvePass.end();

        device.queue.submit([encoder.finish()]);
    };

    const resize = (width: number, height: number): void => {
        aaRenderer.resize(width, height);
        msaaDebugTexture?.destroy();
        resolvedDebugTexture?.destroy();

        // ✅ *** 核心修复：填充完整的纹理描述符 ***
        msaaDebugTexture = device.createTexture({
            label: 'MSAA Debug Value Texture',
            size: [width, height],
            sampleCount: aaRenderer.sampleCount,
            format: 'r32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
        });
        msaaDebugTextureView = msaaDebugTexture.createView();

        // ✅ *** 核心修复：填充完整的纹理描述符 ***
        resolvedDebugTexture = device.createTexture({
            label: 'Resolved Debug Value Texture (for Readback)',
            size: [width, height],
            sampleCount: 1,
            format: 'r32float',
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
        });
        resolvedDebugTextureView = resolvedDebugTexture.createView();

        manualResolveBindGroup = device.createBindGroup({
            label: 'Manual Resolve BindGroup',
            layout: manualResolvePipeline.getBindGroupLayout(0),
            entries: [{ binding: 0, resource: msaaDebugTextureView }],
        });
    };

    const destroy = (): void => {
        aaRenderer?.destroy();
        msaaDebugTexture?.destroy();
        resolvedDebugTexture?.destroy();
        readbackBuffer?.destroy();
        device?.destroy();
    };

    const readDebugValueAt = async (x: number, y: number): Promise<number> => {
        const commandEncoder = device.createCommandEncoder();
        commandEncoder.copyTextureToBuffer(
            { texture: resolvedDebugTexture, origin: [x, y, 0] },
            { buffer: readbackBuffer },
            [1, 1, 1]
        );
        device.queue.submit([commandEncoder.finish()]);
        await readbackBuffer.mapAsync(GPUMapMode.READ);
        const data = new Float32Array(readbackBuffer.getMappedRange());
        const value = data[0];
        readbackBuffer.unmap();
        return value;
    };

    return { device, context, aaRenderer, canvasFormat, render, destroy, resize, readDebugValueAt };
}