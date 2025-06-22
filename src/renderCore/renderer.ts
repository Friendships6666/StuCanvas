import type { IAARenderer } from '../aa/aa-interface';
import { MSAARenderer } from '../aa/msaa-renderer';
import { initializeWebGPU } from './webgpu-helpers';

export interface Renderable {
    draw: (pass: GPURenderPassEncoder) => void;
    compute?: (pass: GPUComputePassEncoder) => void;
    layer?: number;
    // 新增：让Renderable对象可以暴露它需要被清空的纹理
    texturesToClear?: GPUTexture[];
}

export interface Renderer {
    device: GPUDevice;
    context: GPUCanvasContext;
    aaRenderer: IAARenderer;
    canvasFormat: GPUTextureFormat;
    render: (components: Renderable[]) => void;
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

    const render = (components: Renderable[]) => {
        const encoder = device.createCommandEncoder({ label: "Main Command Encoder" });
        const sortedComponents = components.sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));

        // 在所有操作开始前，清空所有需要清空的纹理
        for (const component of sortedComponents) {
            if (component.texturesToClear) {
                for (const texture of component.texturesToClear) {
                    encoder.beginRenderPass({
                        colorAttachments: [{
                            view: texture.createView(),
                            loadOp: 'clear',
                            storeOp: 'store',
                            clearValue: { r: 0, g: 0, b: 0, a: 0 },
                        }],
                    }).end();
                }
            }
        }

        const computePass = encoder.beginComputePass({ label: "Main Compute Pass" });
        for (const component of sortedComponents) {
            component.compute?.(computePass);
        }
        computePass.end();

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