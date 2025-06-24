<!--src/function/render/functionCore.svelte-->
<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { Writable } from 'svelte/store';
    import type { View } from '../../stores/camera';
    import type { Renderable } from '../../renderCore/renderer';
    import { initializeGpuResources, type BatchRendererGpuResources } from '../gpu/functionRenderer';
    import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';

    // --- Props ---
    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: Writable<View>;
    export let aspect: number;
    export let formulas: DrawableFormula[];
    export let requestRender: () => void;
    export let layer: number = 3;
    export let clipOffscreen: boolean = true;

    // --- State ---
    let gpuResources: BatchRendererGpuResources | null = null;

    // ✅ *** 核心修复：添加一个锁来防止并发初始化 ***
    let isInitializing = false;

    // --- 响应式逻辑 1: 当函数列表变化时，重建所有GPU资源 ---
    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        // 如果当前正在初始化，则忽略此调用。
        // 正在进行的初始化最终会使用最新的 `formulas` 数据。
        if (isInitializing) {
            return;
        }

        isInitializing = true; // 上锁

        try {
            // 原子化地捕获并清理旧资源
            const oldResources = gpuResources;
            gpuResources = null;

            if (oldResources) {
                register.delete(oldResources.renderable);
                oldResources.uniformBuffer.destroy();
                oldResources.functionDataBuffer.destroy();
            }

            // 如果没有公式，请求一次渲染以清空画布，然后返回
            if (formulas.length === 0) {
                requestRender();
                return; // 注意：在返回前会进入 finally 解锁
            }

            // 异步创建新资源
            const newResources = await initializeGpuResources(device, canvasFormat, sampleCount, formulas, clipOffscreen);

            // 赋值新资源
            if (newResources) {
                gpuResources = newResources;
                gpuResources.renderable.layer = layer;
                register.add(gpuResources.renderable);
            }
        } catch (e) {
            console.error("Failed during GPU resource initialization:", e);
        } finally {
            isInitializing = false; // 保证在任何情况下都解锁
        }
    }

    // --- 响应式逻辑 2: 当视图或尺寸变化时，更新Uniform缓冲区 ---
    $: if (gpuResources?.uniformBuffer && $view) {
        const yMaxWorld = $view.y + (1.0 / $view.zoom);
        device.queue.writeBuffer(gpuResources.uniformBuffer, 0, new Float32Array([$view.x, $view.y, $view.zoom, aspect]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 32, new Float32Array([yMaxWorld]));
        requestRender();
    }

    // --- 生命周期: 组件销毁时，清理所有资源 ---
    onDestroy(() => {
        if (gpuResources) {
            register.delete(gpuResources.renderable);
            gpuResources.uniformBuffer.destroy();
            gpuResources.functionDataBuffer.destroy();
            gpuResources = null;
        }
    });

</script>