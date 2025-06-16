<script lang="ts">
    import { onDestroy } from 'svelte';
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
    export let formulas: {
        id: string;
        wgsl_expression: string;
        color: { r: number; g: number; b: number; a: number };
    }[];
    export let requestRender: () => void;
    export let layer: number = 3;

    // ✅ 新增: 接收新的 prop，并提供一个默认值
    export let clipOffscreen: boolean = true;

    // --- GPU 资源 ---
    let gpuResources: BatchRendererGpuResources | null = null;

    // 当设备、公式或裁剪设置变化时，重新初始化GPU资源
    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        cleanup();
        if (formulas.length === 0) {
            requestRender();
            return;
        }

        // ✅ 新增: 将 clipOffscreen 传递给 GPU 资源初始化函数
        const resources = await initializeGpuResources(device, canvasFormat, sampleCount, formulas, clipOffscreen);
        if (resources) {
            gpuResources = resources;
            gpuResources.renderable.layer = layer;
            register.add(gpuResources.renderable);
            requestRender();
        }
    }

    // 当视图或画布尺寸变化时，更新uniform缓冲区
    $: if (gpuResources?.uniformBuffer) {
        // ✅ **核心修改**: 计算屏幕上边缘在世界坐标系中的 y 值
        // 公式: 世界坐标y = 视图中心y + (视图半高)
        const yMaxWorld = view.y + (1.0 / view.zoom);

        // 写入已有的 uniform 数据 (前32字节)
        device.queue.writeBuffer(gpuResources.uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));

        // ✅ **核心修改**: 将 yMaxWorld 写入 uniform 缓冲区的扩展空间
        // 偏移量 32 意味着它紧跟在之前的 32 字节数据之后
        device.queue.writeBuffer(gpuResources.uniformBuffer, 32, new Float32Array([yMaxWorld]));

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