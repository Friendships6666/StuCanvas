<!-- src/function/render/functionCore.svelte -->
<script lang="ts">
    import { onDestroy, onMount, type SvelteComponent } from 'svelte';
    import type { View } from '../../stores/camera';
    import type { Renderable } from '../../interaction/input/renderer';
    import { initializeGpuResources, type BatchRendererGpuResources } from '../gpu/gpuInit';

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: View;
    export let aspect: number;
    export let formulas: { id: string; wgsl_expression: string; color: {r:number, g:number, b:number, a:number} }[];
    export let requestRender: () => void;
    export let layer: number = 3;

    // --- GPU 资源 ---
    let gpuResources: BatchRendererGpuResources | null = null;

    // 当设备或公式变化时，重新初始化GPU资源
    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        // 清理旧资源
        cleanup();

        if (formulas.length === 0) {
            requestRender();
            return;
        }

        const resources = await initializeGpuResources(device, canvasFormat, sampleCount, formulas);
        if (resources) {
            gpuResources = resources;
            // 修正 renderable 的 layer
            gpuResources.renderable.layer = layer;
            register.add(gpuResources.renderable);
            requestRender();
        }
    }

    // 当视图或画布尺寸变化时，更新uniform缓冲区
    $: if (gpuResources?.uniformBuffer) {
        device.queue.writeBuffer(gpuResources.uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        requestRender();
    }

    function cleanup() {
        if (gpuResources) {
            register.delete(gpuResources.renderable);
            gpuResources.uniformBuffer.destroy();
            gpuResources.functionDataBuffer.destroy();
            gpuResources = null;
        }
    }

    onDestroy(() => {
        cleanup();
    });

</script>