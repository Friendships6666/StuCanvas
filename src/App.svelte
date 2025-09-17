<script lang="ts">
  import { onMount } from 'svelte';
  import { get } from 'svelte/store';
  import { Renderer } from './lib/webgpu/renderer';
  import FormulaInput from './FormulaInput.svelte';
  import gridShaderCode from './grid.wgsl?raw';
  import { loadPyodideEngine } from './lib/pyodide-loader';

  // 1. 从 stores.ts 导入所有状态
  import { view, canvasSize, functions, gridData, combinedWgslShader, generateRandomColor } from './lib/stores';

  // 2. 从 canvas-interaction.ts 导入交互逻辑
  import { createCanvasInteractionHandlers } from './lib/canvas-interaction';

  // --- 组件内部状态 ---
  let canvasElement: HTMLCanvasElement;
  let renderer: Renderer;
  let isLoading = true;
  let errorMessage: string | null = null;
  let devicePixelRatio = 1;
  let animationFrameRequest: number | null = null;

  // 用于交互处理器的占位符
  let interactionHandlers: ReturnType<typeof createCanvasInteractionHandlers>;

  // --- 生命周期 ---
  onMount(() => {
    devicePixelRatio = window.devicePixelRatio || 1;
    renderer = new Renderer();
    loadPyodideEngine();

    // 3. 创建交互处理器
    interactionHandlers = createCanvasInteractionHandlers(view, canvasSize, devicePixelRatio);

    const resizeObserver = new ResizeObserver(entries => {
      const entry = entries[0];
      const newWidth = Math.round(entry.contentRect.width * devicePixelRatio);
      const newHeight = Math.round(entry.contentRect.height * devicePixelRatio);
      const currentSize = get(canvasSize);
      if (currentSize.width !== newWidth || currentSize.height !== newHeight) {
        canvasElement.width = newWidth;
        canvasElement.height = newHeight;
        canvasSize.set({ width: newWidth, height: newHeight });
      }
    });
    resizeObserver.observe(canvasElement);

    renderer.initialize(canvasElement, get(combinedWgslShader), gridShaderCode).then(success => {
      if (success) {
        // 4. 绑定事件处理器
        canvasElement.addEventListener('mousedown', interactionHandlers.handleMouseDown);
        canvasElement.addEventListener('mouseup', interactionHandlers.handleMouseUp);
        canvasElement.addEventListener('mousemove', interactionHandlers.handleMouseMove);
        canvasElement.addEventListener('mouseleave', interactionHandlers.handleMouseUp);
        canvasElement.addEventListener('wheel', interactionHandlers.handleWheel, { passive: false });
        isLoading = false;
        requestRedraw();
      } else {
        errorMessage = 'Failed to initialize WebGPU Renderer.';
        isLoading = false;
      }
    });

    return () => {
      resizeObserver.disconnect();
      // 清理事件监听器
      if (interactionHandlers) {
        canvasElement.removeEventListener('mousedown', interactionHandlers.handleMouseDown);
        canvasElement.removeEventListener('mouseup', interactionHandlers.handleMouseUp);
        canvasElement.removeEventListener('mousemove', interactionHandlers.handleMouseMove);
        canvasElement.removeEventListener('mouseleave', interactionHandlers.handleMouseUp);
        canvasElement.removeEventListener('wheel', interactionHandlers.handleWheel);
      }
    };
  });

  // --- 函数处理 (现在直接操作导入的 store) ---
  function addFunction() {
    functions.update(fs => [...fs, { id: Date.now(), latex: '', wgsl: '1.0', color: generateRandomColor() }]);
  }

  function removeFunction(id: number) {
    functions.update(fs => fs.filter(f => f.id !== id));
  }

  function updateFunction(id: number, detail: { latex: string, wgsl: string }) {
    functions.update(fs => fs.map(f => f.id === id ? { ...f, ...detail } : f));
  }

  // --- 渲染与交互 ---
  async function handleShaderUpdate(newShader: string) {
    try {
      await renderer.updatePointCloudShader(newShader);
      errorMessage = null;
      requestRedraw();
    } catch (error) {
      errorMessage = `Shader compilation failed: ${error instanceof Error ? error.message : String(error)}`;
    }
  }

  function requestRedraw() {
    if (animationFrameRequest) return;
    animationFrameRequest = requestAnimationFrame(() => {
      if (renderer && canvasElement) {
        renderer.render(
                canvasElement,
                get(view).zoom,
                get(view).offset,
                get(functions).length,
                get(functions).map(f => f.color),
                get(gridData)
        );
      }
      animationFrameRequest = null;
    });
  }

  // --- 响应式语句 ---
  $: if (renderer?.isInitialized) {
    handleShaderUpdate($combinedWgslShader);
  }

  $: if (renderer?.isInitialized && ($view || $canvasSize)) {
    requestRedraw();
  }
</script>

<main>
  <div class="main-container">
    <div class="control-panel">
      <h1>StuCanvas</h1>

      {#each $functions as func (func.id)}
        <div class="function-input-container">
          <div class="color-indicator" style="background-color: rgb({func.color[0] * 255}, {func.color[1] * 255}, {func.color[2] * 255});"></div>
          <div class="formula-input-wrapper">
            <FormulaInput
                    value={func.latex}
                    on:update={(e) => updateFunction(func.id, e.detail)}
            />
          </div>
          <button class="remove-btn" on:click={() => removeFunction(func.id)} title="Remove function">×</button>
        </div>
      {/each}

      <button class="add-btn" on:click={addFunction}>+ Add Function</button>
    </div>

    <div class="canvas-panel">
      {#if isLoading}<p class="loading-overlay">Initializing WebGPU...</p>{:else if errorMessage}<div class="error-overlay"><h1>Error</h1><p>{errorMessage}</p></div>{/if}
      <div class="canvas-container">
        <canvas bind:this={canvasElement} />

        <div class="label-overlay">
          {#if interactionHandlers && $gridData && $gridData.labels}
            {#each $gridData.labels as label (label.id)}
              {@const pos = interactionHandlers.worldToScreen(label.worldX, label.worldY)}
              {@const labelClass = `grid-label ${label.orientation}`}
              {@const finalX = Math.round(pos.x / devicePixelRatio)}
              {@const finalY = Math.round(pos.y / devicePixelRatio)}

              <div
                      class={labelClass}
                      style="translate: {finalX}px {finalY}px;"
              >
                {label.text}
              </div>
            {/each}
          {/if}
        </div>
      </div>
    </div>
  </div>
</main>

<style>
  /* 样式部分保持不变 */
  :global(html, body) { margin: 0; padding: 0; overflow: hidden; width: 100%; height: 100%; font-family: system-ui, sans-serif; }
  main { width: 100%; height: 100%; }
  .main-container { display: flex; width: 100vw; height: 100vh; }
  .control-panel { width: 450px; padding: 1rem; overflow-y: auto; border-right: 1px solid #ccc; background-color: #f9f9f9; box-sizing: border-box; }
  .canvas-panel { flex-grow: 1; position: relative; }
  .canvas-container { position: relative; width: 100%; height: 100%; }
  canvas { width: 100%; height: 100%; display: block; background-color: #FFFFFF; cursor: grab; }
  canvas:active { cursor: grabbing; }
  .function-input-container { display: flex; align-items: center; margin-bottom: 0.75rem; gap: 0.5rem; }
  .color-indicator { width: 12px; height: 24px; border-radius: 4px; flex-shrink: 0; border: 1px solid rgba(0,0,0,0.2); }
  .formula-input-wrapper { flex-grow: 1; }
  .remove-btn { border: none; background: #f0f0f0; color: #555; font-weight: bold; border-radius: 50%; width: 24px; height: 24px; cursor: pointer; line-height: 22px; text-align: center; flex-shrink: 0; transition: background-color 0.2s, color 0.2s; }
  .remove-btn:hover { background-color: #e57373; color: white; }
  .add-btn { width: 100%; padding: 0.5rem; margin-top: 1rem; background-color: #e0e0e0; border: 1px solid #ccc; border-radius: 4px; cursor: pointer; font-size: 1rem; }
  .add-btn:hover { background-color: #d5d5d5; }
  .loading-overlay { position: absolute; top: 0; left: 0; width: 100%; height: 100%; display: flex; justify-content: center; align-items: center; background-color: rgba(255, 255, 255, 0.8); font-size: 2rem; font-weight: bold; color: #333; z-index: 10; }
  .error-overlay { position: absolute; top: 2rem; left: 2rem; background-color: #ffcccc; border: 1px solid #ff0000; color: #330000; padding: 1rem; max-width: 80%; font-family: monospace; border-radius: 4px; z-index: 20; }
  .label-overlay { position: absolute; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; overflow: hidden; }
  .grid-label { position: absolute; top: 0; left: 0; color: #666; font-family: Arial, sans-serif; font-size: 13px; text-shadow: 1px 1px 0 #fff, -1px 1px 0 #fff, 1px -1px 0 #fff, -1px -1px 0 #fff; white-space: nowrap; user-select: none; }
  .grid-label.horizontal { transform: translate(calc(-100% - 5px), -50%); }
  .grid-label.vertical { transform: translate(-50%, 8px); }
  .grid-label.origin { transform: translate(calc(-100% - 4px), 4px); }
</style>