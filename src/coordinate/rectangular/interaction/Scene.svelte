<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { initializeRenderer, type Renderer, type Renderable } from './renderer';
    import { initializeInputHandlers, type InputHandler } from './inputHandler';
    import { view } from '../../../stores/camera';
    import { rightMenu, formulas } from '../../../stores/ui';
    import { calculateVisibleGridLines, type LabelData, type WorldBounds } from '../logic/grid';
    import { create, all } from 'mathjs';
    import hash from 'object-hash'; // 使用 object-hash 生成稳定 ID
    import { schemeCategory10 } from 'd3-scale-chromatic'; // 引入 d3-scale-chromatic
    import { scaleOrdinal } from 'd3-scale';

    import GridLineRenderer from '../../../geometry/GridLineRenderer.svelte';
    // ✅ 引入新的批量渲染器
    import ImplicitFunctionBatchRenderer from '../../../geometry/ImplicitFunctionBatchRenderer.svelte';
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

    // ✅ 修改 drawableFormulas 的结构，为每个函数分配一个持久且美观的颜色
    let drawableFormulas: { id: string; wgsl_expression: string; color: {r:number, g:number, b:number, a:number} }[] = [];
    const colorScale = scaleOrdinal(schemeCategory10); // 创建一个序数颜色比例尺

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

    // ✅ 修改公式解析逻辑，为每个公式生成颜色
    $: {
        const newFormulas = $formulas.map(f => {
            try {
                const formulaStr = f.trim();
                if (!formulaStr) return null;

                const eqIndex = formulaStr.indexOf('=');
                let combinedExpression: string;

                if (eqIndex !== -1) {
                    const leftPart = formulaStr.substring(0, eqIndex).trim();
                    const rightPart = formulaStr.substring(eqIndex + 1).trim();
                    combinedExpression = `(${leftPart}) - (${rightPart})`;
                } else {
                    combinedExpression = `y - (${formulaStr})`;
                }

                const parsedNode = math.parse(combinedExpression);
                const wgslExpression = parsedNode.toString();

                // 使用公式字符串本身作为颜色比例尺的输入，以获得一致的颜色
                const hexColor = colorScale(formulaStr);
                const r = parseInt(hexColor.slice(1, 3), 16) / 255;
                const g = parseInt(hexColor.slice(3, 5), 16) / 255;
                const b = parseInt(hexColor.slice(5, 7), 16) / 255;

                return {
                    id: hash(formulaStr), // 使用 hash 生成唯一 ID
                    wgsl_expression: wgslExpression,
                    color: { r, g, b, a: 0.9 } // 设置颜色和透明度
                };
            } catch (e) {
                console.warn(`公式 "${f}" 解析失败:`, e);
                return null;
            }
        }).filter(Boolean);

        // 仅在内容实际变化时更新，避免不必要的重渲染
        if (hash(newFormulas) !== hash(drawableFormulas)) {
            drawableFormulas = newFormulas as any;
        }
    }


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

<!-- HTML部分 -->

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

            <!-- ✅ 使用新的批量渲染器，替换掉旧的 #each 循环 -->
            <ImplicitFunctionBatchRenderer
                    register={renderables}
                    formulas={drawableFormulas}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
                    canvasElement={canvas}
                    view={$view}
                    {aspect}
                    {requestRender}
            />
        {/if}
    </div>

    <RightMenu />
    <AlgebraWindow />

</div>

<!-- Style部分不变 -->

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