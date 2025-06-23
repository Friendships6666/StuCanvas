<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { Renderable } from '../../renderCore/renderer';
    import { initializeMaskResources, type MaskGpuResources } from './mask_resources';

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let layer: number = 4;
    // ✅ *** 核心修正 1: 重新接收 requestRender 作为 prop ***
    export let requestRender: () => void;

    // --- State ---
    let gpuResources: MaskGpuResources | null = null;
    let isInitializing = false;

    // --- 响应式逻辑 ---
    $: if (device && canvasElement && !gpuResources) {
        initialize();
    }

    async function initialize() {
        if (isInitializing) return;
        isInitializing = true;
        cleanup();
        try {
            const newResources = await initializeMaskResources(device, canvasFormat, sampleCount, canvasElement);
            gpuResources = newResources;
            gpuResources.renderable.layer = layer;
            register.add(gpuResources.renderable);

            // ✅ *** 核心修正 2: 在准备就绪后，主动请求一次渲染 ***
            // 这解决了父组件首次渲染时，子组件尚未注册的问题。
            requestRender();

        } catch (e) {
            console.error("Failed to initialize scatter mask resources:", e);
        } finally {
            isInitializing = false;
        }
    }

    // --- 生命周期 ---
    function cleanup() {
        if (gpuResources) {
            register.delete(gpuResources.renderable);
            gpuResources.maskTexture.destroy();
            gpuResources.uniformBuffer.destroy();
            gpuResources = null;
        }
    }

    onDestroy(() => {
        cleanup();
    });
</script>