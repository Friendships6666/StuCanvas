<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { Renderable } from '../../renderCore/renderer';
    import { initializeMaskRendererFs, type MaskRendererFsResources } from './mask_renderer_fs';

    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let requestRender: () => void;
    export let layer: number = 4;

    let gpuResources: MaskRendererFsResources | null = null;
    let isInitializing = false;

    $: if (device && !gpuResources) {
        initialize();
    }

    async function initialize() {
        if (isInitializing) return;
        isInitializing = true;
        cleanup();
        try {
            const newResources = await initializeMaskRendererFs(device, canvasFormat, sampleCount);
            gpuResources = newResources;
            gpuResources.renderable.layer = layer;
            register.add(gpuResources.renderable);
            requestRender();
        } catch (e) {
            console.error("Failed to initialize FS mask renderer:", e);
        } finally {
            isInitializing = false;
        }
    }

    function cleanup() {
        if (gpuResources) {
            register.delete(gpuResources.renderable);
            gpuResources.uniformBuffer.destroy();
            gpuResources = null;
        }
    }

    onDestroy(() => {
        cleanup();
    });
</script>