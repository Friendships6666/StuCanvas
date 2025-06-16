<!--src/coordinate/rectangular/use/rectangularCore.svelte-->
<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { writable } from 'svelte/store';
    import { initializeRenderer, type Renderer, type Renderable } from '../../../interaction/input/renderer';
    import { initializeInputHandlers, type InputHandler } from '../../../interaction/input/inputHandler';
    import { view } from '../../../stores/camera';
    import { rightMenu, formulas } from '../../../stores/ui';

    // 导入新的 hooks
    import { useGridLines } from './use-grid-lines';
    import { useFormulas } from './use-formulas';

    // 导入子组件
    import GridLineRenderer from '../grid/GridLineRenderer.svelte';
    import FunctionCore from '../../../function/render/functionCore.svelte';
    import RightMenu from '../../../interaction/menu/RightMenu.svelte';
    import AlgebraWindow from '../../../interaction/menu/AlgebraWindow.svelte';
    import Label from '../label/Label.svelte';

    let canvas: HTMLCanvasElement;
    let renderer: Renderer | null = null;
    let inputHandler: InputHandler | null = null;
    let resizeObserver: ResizeObserver;
    const aspect = writable(1.0);

    let renderables = new Set<Renderable>();

    // 使用 hooks 获取响应式数据
    const gridData = useGridLines(view, aspect);
    const drawableFormulas = useFormulas(formulas);

    // 渲染请求函数
    function requestRender() {
        if (renderer) {
            const sortedRenderables = Array.from(renderables).sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));
            requestAnimationFrame(() => renderer?.render(sortedRenderables));
        }
    }

    // 组件生命周期
    onMount(async () => {
        const init = await initializeRenderer(canvas);
        if (!init) return;
        renderer = init;
        inputHandler = initializeInputHandlers(canvas, () => $aspect, requestRender);

        view.subscribe(() => requestRender());

        resizeObserver = new ResizeObserver(entries => {
            for (const entry of entries) {
                const { width, height } = entry.contentRect;
                if (width > 0 && height > 0) {
                    canvas.width = width;
                    canvas.height = height;
                    aspect.set(width / height); // 更新 aspect store
                    renderer?.resize(width, height);
                    requestRender();
                }
            }
        });
        if (canvas.parentElement) resizeObserver.observe(canvas.parentElement);
        requestRender();
    });

    onDestroy(() => {
        if (resizeObserver) resizeObserver.disconnect();
        inputHandler?.destroy();
        renderer?.destroy();
    });

    // 右键菜单处理
    function handleContextMenu(e: MouseEvent) {
        e.preventDefault();
        rightMenu.set({ visible: true, x: e.clientX, y: e.clientY });
    }
</script>

<div class="scene-container" on:contextmenu={handleContextMenu} role="application">
    <canvas bind:this={canvas}></canvas>

    <!-- 标签覆盖层 -->
    <div class="overlay-container">
        {#each $gridData.allLabels as label (label.id)}
            <Label {...label} aspect={$aspect} />
        {/each}
    </div>

    <!-- 逻辑渲染组件 -->
    <div class="logical-components">
        {#if renderer}
            <!-- 网格线渲染 -->
            <GridLineRenderer register={renderables} layer={0} color="#e0e0e0" vertices={$gridData.minorVertices} device={renderer.device} canvasFormat={renderer.canvasFormat} sampleCount={renderer.aaRenderer.sampleCount} />
            <GridLineRenderer register={renderables} layer={1} color="#cccccc" vertices={$gridData.majorVertices} device={renderer.device} canvasFormat={renderer.canvasFormat} sampleCount={renderer.aaRenderer.sampleCount} />
            <GridLineRenderer register={renderables} layer={2} color="#333333" vertices={$gridData.axisVertices} device={renderer.device} canvasFormat={renderer.canvasFormat} sampleCount={renderer.aaRenderer.sampleCount} />

            <!-- 隐函数渲染 -->
            <FunctionCore register={renderables} formulas={$drawableFormulas} device={renderer.device} canvasFormat={renderer.canvasFormat} sampleCount={renderer.aaRenderer.sampleCount} canvasElement={canvas} view={$view} aspect={$aspect} {requestRender} />
            <FunctionCore
                    register={renderables}
                    formulas={$drawableFormulas}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
                    canvasElement={canvas}
                    view={$view}
                    aspect={$aspect}
                    {requestRender}
                    clipOffscreen={true}
            />
        {/if}
    </div>

    <!-- UI 组件 -->
    <RightMenu />
    <AlgebraWindow />
</div>

<style>
    .scene-container {
        position: fixed; top: 0; left: 0;
        width: 100vw; height: 100vh;
        overflow: hidden; background-color: #ffffff;
    }
    canvas {
        display: block; width: 100%; height: 100%;
        cursor: grab;
    }
    canvas:active { cursor: grabbing; }
    .overlay-container, .logical-components {
        position: absolute; top: 0; left: 0;
        width: 100%; height: 100%;
        pointer-events: none;
    }
    .logical-components { visibility: hidden; }
</style>