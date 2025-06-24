<script lang="ts">
    import {onMount , onDestroy} from 'svelte';
    import {writable} from 'svelte/store';
    import {initializeRenderer , type Renderer , type Renderable} from '../../../renderCore/renderer';
    import {initializeInputHandlers , type InputHandler} from '../../../interaction/input/inputHandler';
    import {view} from '../../../stores/camera';
    import {rightMenu , formulas} from '../../../stores/ui';

    // 导入我们强大的响应式 "hooks"
    import {useGridLines} from './use-grid-lines';
    import {useFormulas} from './use-formulas';

    // 导入所有子组件
    import GridLineRenderer from '../grid/GridLineRenderer.svelte';
    import FunctionCore from '../../../function/render/functionCore.svelte';
    import RightMenu from '../../../interaction/menu/RightMenu.svelte';
    import AlgebraWindow from '../../../interaction/menu/AlgebraWindow.svelte';
    import Label from '../label/Label.svelte';

    // --- 组件内部状态 ---
    let canvas : HTMLCanvasElement;
    let renderer : Renderer | null = null;
    let inputHandler : InputHandler | null = null;
    let resizeObserver : ResizeObserver;
    const aspect = writable ( 1.0 );

    // 一个 Set，用于收集所有需要被渲染的对象
    let renderables = new Set<Renderable> ();

    // ✅ --- Debugging State ---
    let isDebugMode = false;
    let discriminantValue: string | null = null;

    // --- 响应式数据流 ---
    // 当 view 或 aspect 变化时，gridData 会自动重新计算
    const gridData = useGridLines ( view , aspect );
    // 当 formulas store 变化时，drawableFormulas 会自动重新解析和重构
    const drawableFormulas = useFormulas ( formulas );

    // --- 核心渲染函数 ---
    function requestRender () {
        if ( renderer ) {
            // 按 layer 排序，确保渲染顺序正确
            const sortedRenderables = Array.from ( renderables ).sort ( ( a , b ) => (a.layer ?? 0) - (b.layer ?? 0) );
            requestAnimationFrame ( () => renderer?.render ( sortedRenderables ) );
        }
    }

    // --- 组件生命周期管理 ---
    onMount ( async () => {
        const init = await initializeRenderer ( canvas );
        if ( !init ) return;
        renderer = init;

        // ✅ *** 核心修复 ***
        // 初始化 inputHandler，并传入一个新的回调函数来接收调试值
        inputHandler = initializeInputHandlers (
            canvas,
            renderer,
            () => $aspect,
            requestRender,
            (value) => { // 这个回调会在 inputHandler 读取到值后被调用
                // 只有在调试模式开启时才更新UI，避免不必要的状态变化
                if (isDebugMode) {
                    discriminantValue = value.toFixed(4);
                }
            }
        );

        // 订阅 view store 的变化，任何相机移动都触发重绘
        view.subscribe ( () => requestRender () );

        // 监听父容器尺寸变化，实现响应式布局
        resizeObserver = new ResizeObserver ( entries => {
            for (const entry of entries) {
                const {
                    width ,
                    height
                } = entry.contentRect;
                if ( width > 0 && height > 0 ) {
                    canvas.width = width;
                    canvas.height = height;
                    aspect.set ( width / height ); // 更新 aspect store
                    renderer?.resize ( width , height );
                    requestRender ();
                }
            }
        } );
        if ( canvas.parentElement ) resizeObserver.observe ( canvas.parentElement );
        requestRender ();
    } );

    onDestroy ( () => {
        // 销毁所有资源，防止内存泄漏
        if ( resizeObserver ) resizeObserver.disconnect ();
        inputHandler?.destroy ();
        renderer?.destroy ();
    } );

    // --- UI 事件处理 ---
    function handleContextMenu ( e : MouseEvent ) {
        e.preventDefault ();
        rightMenu.set ( {
            visible : true ,
            x : e.clientX ,
            y : e.clientY
        } );
    }
</script>

<div class="scene-container" on:contextmenu={handleContextMenu} role="application">
    <canvas bind:this={canvas}></canvas>

    <!-- 标签和UI覆盖层 -->
    <div class="overlay-container">
        {#each $gridData.allLabels as label (label.id)}
            <Label {...label} aspect={$aspect}/>
        {/each}

        <div class="zoom-display">
            Zoom: {$view.zoom.toFixed(7)}x
        </div>

        <!-- ✅ UI for Debugging -->
        <div class="debug-display">
            <label>
                <input type="checkbox" bind:checked={isDebugMode} />
                Show Discriminant
            </label>
            {#if isDebugMode && discriminantValue !== null}
                <div class="debug-value">Value: {discriminantValue}</div>
            {/if}
        </div>
    </div>

    <!-- 逻辑渲染组件 (不可见，仅用于挂载和传递props) -->
    <div class="logical-components">
        {#if renderer}
            <!-- 网格线渲染 -->
            <GridLineRenderer register={renderables} layer={0} color="#e0e0e0" vertices={$gridData.minorVertices}
                              device={renderer.device} canvasFormat={renderer.canvasFormat}
                              sampleCount={renderer.aaRenderer.sampleCount}/>
            <GridLineRenderer register={renderables} layer={1} color="#cccccc" vertices={$gridData.majorVertices}
                              device={renderer.device} canvasFormat={renderer.canvasFormat}
                              sampleCount={renderer.aaRenderer.sampleCount}/>
            <GridLineRenderer register={renderables} layer={2} color="#333333" vertices={$gridData.axisVertices}
                              device={renderer.device} canvasFormat={renderer.canvasFormat}
                              sampleCount={renderer.aaRenderer.sampleCount}/>

            <!-- 隐函数渲染 -->
            <FunctionCore
                    register={renderables}
                    formulas={$drawableFormulas}
                    device={renderer.device}
                    canvasFormat={renderer.canvasFormat}
                    sampleCount={renderer.aaRenderer.sampleCount}
                    canvasElement={canvas}
                    view={view}
                    aspect={$aspect}
                    {requestRender}
                    clipOffscreen={true}
                    bind:isDebugMode={isDebugMode}
            />
        {/if}
    </div>

    <!-- UI 窗口组件 -->
    <RightMenu/>
    <AlgebraWindow/>
</div>

<style>
    .scene-container {
        position: fixed;
        top: 0;
        left: 0;
        width: 100vw;
        height: 100vh;
        overflow: hidden;
        background-color: #ffffff;
    }

    canvas {
        display: block;
        width: 100%;
        height: 100%;
        cursor: grab;
    }

    canvas:active {
        cursor: grabbing;
    }

    .overlay-container, .logical-components {
        position: absolute;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        pointer-events: none;
    }

    .logical-components {
        visibility: hidden;
    }

    .zoom-display {
        position: absolute;
        bottom: 20px;
        left: 20px;
        background-color: rgba(0, 0, 0, 0.65);
        color: white;
        padding: 6px 12px;
        border-radius: 6px;
        font-family: monospace, sans-serif;
        font-size: 14px;
        user-select: none;
        z-index: 10;
    }

    .debug-display {
        position: absolute;
        top: 10px;
        left: 10px;
        background-color: rgba(0, 0, 0, 0.65);
        color: white;
        padding: 6px 12px;
        border-radius: 6px;
        font-family: monospace, sans-serif;
        font-size: 14px;
        user-select: none;
        z-index: 10;
        pointer-events: auto; /* 允许点击复选框 */
    }

    .debug-value {
        margin-top: 5px;
    }
</style>