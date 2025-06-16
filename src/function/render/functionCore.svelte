<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { View } from '../../stores/camera';
    import type { Renderable } from '../../interaction/input/renderer';
    import { initializeGpuResources, type BatchRendererGpuResources } from '../gpu/gpuInit';
    import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';

    // --- Props ---
    /**
     * 一个 Set，用于将此组件的渲染逻辑注册到主渲染器中。
     */
    export let register: Set<Renderable>;

    /**
     * WebGPU 设备实例。
     */
    export let device: GPUDevice;

    /**
     * 画布的纹理格式。
     */
    export let canvasFormat: GPUTextureFormat;

    /**
     * 多重采样抗锯齿 (MSAA) 的采样数。
     */
    export let sampleCount: number;

    /**
     * 用于渲染的 HTML canvas 元素。
     */
    export let canvasElement: HTMLCanvasElement;

    /**
     * 相机的视图属性 (位置和缩放)。
     */
    export let view: View;

    /**
     * 画布的宽高比。
     */
    export let aspect: number;

    /**
     * 一个包含可绘制公式对象的数组。
     * 这是从 use-formulas.ts 传递过来的，包含了所有解析和重构后的信息。
     */
    export let formulas: DrawableFormula[];

    /**
     * 一个回调函数，用于请求一次新的渲染帧。
     */
    export let requestRender: () => void;

    /**
     * 此组件的渲染层级。
     */
    export let layer: number = 3;

    /**
     * 是否启用屏幕外裁剪功能的开关。
     */
    export let clipOffscreen: boolean = true;

    // --- GPU 资源 ---
    let gpuResources: BatchRendererGpuResources | null = null;

    // --- 响应式逻辑 ---
    // 当设备、公式或裁剪设置发生变化时，重新初始化所有GPU资源。
    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        // 在初始化新资源前，先清理旧的资源。
        cleanup();

        if (formulas.length === 0) {
            requestRender(); // 请求一次渲染以清空画布
            return;
        }

        // 异步初始化 GPU 缓冲区、渲染管线等，并将 clipOffscreen 设置传递下去。
        const resources = await initializeGpuResources(device, canvasFormat, sampleCount, formulas, clipOffscreen);
        if (resources) {
            gpuResources = resources;
            // 设置渲染层级并注册到主渲染器
            gpuResources.renderable.layer = layer;
            register.add(gpuResources.renderable);
            requestRender(); // 请求一次渲染来绘制新的公式
        }
    }

    // 当视图或画布尺寸变化时，更新 Uniform 缓冲区。
    $: if (gpuResources?.uniformBuffer) {
        // 计算屏幕上边缘在世界坐标系中的 y 值，用于裁剪
        const yMaxWorld = view.y + (1.0 / view.zoom);

        // 写入 Uniform 数据
        device.queue.writeBuffer(gpuResources.uniformBuffer, 0, new Float32Array([view.x, view.y, view.zoom, aspect]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 16, new Float32Array([canvasElement.width, canvasElement.height]));
        device.queue.writeBuffer(gpuResources.uniformBuffer, 32, new Float32Array([yMaxWorld]));

        requestRender();
    }

    /**
     * 清理并销毁所有由该组件创建的 GPU 资源。
     */
    function cleanup() {
        if (gpuResources) {
            register.delete(gpuResources.renderable);
            gpuResources.uniformBuffer.destroy();
            gpuResources.functionDataBuffer.destroy();
            // 注意：渲染管线由 gpuInit 缓存，不应在此处销毁
            gpuResources = null;
        }
    }

    // 确保在组件被销毁时调用 cleanup 函数，防止内存泄漏。
    onDestroy(() => {
        cleanup();
    });

</script>

<!-- 这个组件是纯逻辑的，没有任何需要渲染到 DOM 的 HTML 元素 -->