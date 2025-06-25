<!--src/function/render/functionCore.svelte-->
<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { Writable } from 'svelte/store';
    import type { View } from '../../stores/camera';
    import type { Renderable } from '../../renderCore/renderer';
    import { initializeGpuResources, type BatchRendererGpuResources } from '../SDF/functionRenderer';
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

    // ✅ *** 核心修复 ***
    // 声明此组件接收 isDebugMode 这个 prop，并提供一个默认值。
    // 'bind:isDebugMode' 在父组件中会将父组件的值同步到这里。
    export let isDebugMode: boolean = false;

    // --- State ---
    let gpuResources: BatchRendererGpuResources | null = null;
    let isInitializing = false;

    // --- 响应式逻辑 1: 当函数列表变化时，重建所有GPU资源 ---
    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        if (isInitializing) {
            return;
        }
        isInitializing = true;

        try {
            const oldResources = gpuResources;
            gpuResources = null;

            if (oldResources) {
                register.delete(oldResources.renderable);
                oldResources.uniformBuffer.destroy();
                oldResources.functionDataBuffer.destroy();
            }

            if (formulas.length === 0) {
                requestRender();
                return;
            }

            const newResources = await initializeGpuResources(device, canvasFormat, sampleCount, formulas, clipOffscreen);

            if (newResources) {
                gpuResources = newResources;
                gpuResources.renderable.layer = layer;
                register.add(gpuResources.renderable);
            }
        } catch (e) {
            console.error("Failed during GPU resource initialization:", e);
        } finally {
            isInitializing = false;
        }
    }

    // --- 响应式逻辑 2: 当视图、尺寸或调试模式变化时，更新Uniform缓冲区 ---
    // isDebugMode 现在是这个响应式块的依赖项之一
    $: if (gpuResources?.uniformBuffer && $view) {
        const yMaxWorld = $view.y + (1.0 / $view.zoom);

        // 写入视图、画布和裁剪参数
        device.queue.writeBuffer(gpuResources.uniformBuffer, 0, new Float32Array([$view.x, $view.y, $view.zoom, aspect]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 32, new Float32Array([yMaxWorld]));

        // ✅ 将 isDebugMode 的当前值写入 uniform buffer 的正确偏移位置
        device.queue.writeBuffer(gpuResources.uniformBuffer, 48, new Float32Array([isDebugMode ? 1.0 : 0.0]));

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

<!--
    这个组件是一个“逻辑组件”，它不渲染任何可见的 HTML。
    它的全部工作都在 <script> 标签里完成：创建和管理 GPU 资源，
    并将自己的绘制能力注册到外部系统。
-->