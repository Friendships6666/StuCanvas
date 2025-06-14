<!-- src/coordinate/rectangular/interaction/Scene.svelte (Final Correct Version) -->
<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { initializeRenderer, type Renderer, type Renderable } from './renderer';
    import { initializeInputHandlers, type InputHandler } from './inputHandler';
    import { view } from '../../../stores/camera';
    import { rightMenu, formulas } from '../../../stores/ui';
    import { calculateVisibleGridLines, type LabelData, type WorldBounds } from '../logic/grid';
    import { create, all } from 'mathjs';

    import GridLineRenderer from '../../../geometry/GridLineRenderer.svelte';
    import FunctionGraph from '../../../geometry/FunctionGraph.svelte';
    import RightMenu from '../../../rightMenu/RightMenu.svelte';
    import AlgebraWindow from '../../../rightMenu/AlgebraWindow.svelte';
    import Label from '../label/Label.svelte';

    const math = create(all);
    let canvas: HTMLCanvasElement;
    let renderer: Renderer | null = null;
    let inputHandler: InputHandler | null = null;
    let resizeObserver: ResizeObserver;
    let aspect = 1.0;

    let renderables = new Set<Renderable>();

    let minorVertices = new Float32Array(0);
    let majorVertices = new Float32Array(0);
    let axisVertices = new Float32Array(0);
    let allLabels: LabelData[] = [];
    let drawableFormulas: any[] = [];

    $: {
        const { zoom, x: viewX, y: viewY } = $view;
        if (canvas && canvas.width > 0 && canvas.height > 0) {
            const world_x_radius = aspect / zoom;
            const world_y_radius = 1.0 / zoom;
            const worldBounds: WorldBounds = {
                left: viewX - world_x_radius, right: viewX + world_x_radius,
                top: viewY + world_y_radius, bottom: viewY - world_y_radius,
            };
            const { lines, labels } = calculateVisibleGridLines(zoom, worldBounds);
            allLabels = labels;
            const temp: { minor: number[], major: number[], axis: number[] } = { minor: [], major: [], axis: [] };
            for (const line of lines) {
                const { position: p, orientation, lineWidth, layer } = line;
                const half_width = (lineWidth / 2) / zoom;
                let p1, p2, p3, p4;
                if (orientation === 'horizontal') {
                    p1 = { x: worldBounds.left, y: p - half_width }; p2 = { x: worldBounds.right, y: p - half_width };
                    p3 = { x: worldBounds.right, y: p + half_width }; p4 = { x: worldBounds.left, y: p + half_width };
                } else {
                    p1 = { x: p - half_width, y: worldBounds.bottom }; p2 = { x: p + half_width, y: worldBounds.bottom };
                    p3 = { x: p + half_width, y: worldBounds.top }; p4 = { x: p - half_width, y: worldBounds.top };
                }
                const c1 = { x: (p1.x - viewX) * zoom / aspect, y: (p1.y - viewY) * zoom };
                const c2 = { x: (p2.x - viewX) * zoom / aspect, y: (p2.y - viewY) * zoom };
                const c3 = { x: (p3.x - viewX) * zoom / aspect, y: (p3.y - viewY) * zoom };
                const c4 = { x: (p4.x - viewX) * zoom / aspect, y: (p4.y - viewY) * zoom };
                const rect_verts = [c1.x, c1.y, c2.x, c2.y, c3.x, c3.y, c1.x, c1.y, c3.x, c3.y, c4.x, c4.y];
                if (layer === 0) temp.minor.push(...rect_verts);
                else if (layer === 1) temp.major.push(...rect_verts);
                else temp.axis.push(...rect_verts);
            }
            minorVertices = new Float32Array(temp.minor);
            majorVertices = new Float32Array(temp.major);
            axisVertices = new Float32Array(temp.axis);
        }
    }

    // ✅ 简化公式解析逻辑，不再需要导函数
    $: drawableFormulas = $formulas.map(f => {
        try {
            const formulaStr = f.trim();
            const parts = formulaStr.split('=').map(p => p.trim());
            if (parts.length !== 2) return null;

            const [left, right] = parts;
            if (left === 'y' && right) {
                math.parse(right); // 仅用 math.js 验证语法
                return {
                    id: formulaStr,
                    expression: right,
                    left_side: left
                };
            }
            return null;
        } catch (e) {
            console.warn(`无法解析公式: "${f}"`, e);
            return null;
        }
    }).filter(Boolean);

    function requestRender() {
        if (renderer) {
            const sortedRenderables = Array.from(renderables).sort((a, b) => (a.layer ?? 0) - (b.layer ?? 0));
            requestAnimationFrame(() => renderer?.render(sortedRenderables));
        }
    }

    onMount(async () => {
        const init = await initializeRenderer(canvas);
        if (!init) return;
        renderer = init;
        inputHandler = initializeInputHandlers(canvas, () => aspect, requestRender);
        view.subscribe(() => requestRender());
        resizeObserver = new ResizeObserver(entries => {
            for (const entry of entries) {
                const { width, height } = entry.contentRect;
                if (width > 0 && height > 0) {
                    canvas.width = width; canvas.height = height;
                    aspect = width / height;
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

    function handleContextMenu(e: MouseEvent) {
        e.preventDefault();
        rightMenu.set({ visible: true, x: e.clientX, y: e.clientY });
    }
</script>

<!-- HTML部分完全不变 -->
<div class="scene-container" on:contextmenu={handleContextMenu} role="application">
    <canvas bind:this={canvas}></canvas>

    <div class="overlay-container">
        {#each allLabels as label (label.id)}
            <Label {...label} {aspect} />
        {/each}
    </div>

    <div class="logical-components">
        {#if renderer}
            <GridLineRenderer
                    register={renderables}
                    layer={0} color="#e0e0e0"
                    vertices={minorVertices}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
            />
            <GridLineRenderer
                    register={renderables}
                    layer={1} color="#cccccc"
                    vertices={majorVertices}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
            />
            <GridLineRenderer
                    register={renderables}
                    layer={2} color="#000000"
                    vertices={axisVertices}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
            />

            {#each drawableFormulas as formulaObj (formulaObj.id)}
                <FunctionGraph
                        register={renderables}
                        formula={formulaObj}
                        device={renderer.device}
                        canvasFormat={renderer.canvasFormat}
                        sampleCount={renderer.aaRenderer.sampleCount}
                        canvasElement={canvas}
                        view={$view}
                        {aspect}
                        {requestRender}
                />
            {/each}
        {/if}
    </div>

    <RightMenu />
    <AlgebraWindow />
</div>

<!-- Style部分完全不变 -->
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