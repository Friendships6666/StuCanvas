<script lang="ts">
    // ✅ 1. 确保 onMount 被导入，以便添加生命周期日志
    import { onMount } from 'svelte';
    import { algebraWindowVisible, formulas } from '../stores/ui';

    // --- 数据同步逻辑 ---

    // 将 store 中的公式数组转换成用换行符连接的单个字符串，用于显示在 textarea 中
    let formulaText: string;
    $: formulaText = $formulas.join('\n');

    // 当用户在 textarea 中输入时，将字符串分割成数组，更新回 store
    function handleInput(e: Event) {
        const target = e.currentTarget as HTMLTextAreaElement;
        // 使用 a.trim() !== '' 来过滤掉因多余换行产生的空公式
        $formulas = target.value.split('\n').filter(f => f.trim() !== '');
    }


    // --- 窗口拖动逻辑 ---
    let windowEl: HTMLDivElement;
    let isDragging = false;
    let initialX: number, initialY: number;
    // 窗口的初始位置和当前位置
    let offsetX = 20; // 初始 X 坐标 (距离左边)
    let offsetY = 120; // 初始 Y 坐标 (距离顶部)

    function onPointerDown(e: PointerEvent) {
        // 只在点击窗口头部时才开始拖动
        if ((e.target as HTMLElement).closest('.window-header')) {
            isDragging = true;
            // 记录鼠标初始位置和窗口当前位置的差值
            initialX = e.clientX - offsetX;
            initialY = e.clientY - offsetY;

            // 在 window 上添加事件监听器，以便鼠标移出窗口也能继续拖动
            window.addEventListener('pointermove', onPointerMove);
            window.addEventListener('pointerup', onPointerUp);
        }
    }

    function onPointerMove(e: PointerEvent) {
        if (isDragging) {
            e.preventDefault(); // 阻止拖动时选中文本等默认行为
            // 根据鼠标的移动更新窗口的位置
            offsetX = e.clientX - initialX;
            offsetY = e.clientY - initialY;
        }
    }

    function onPointerUp() {
        if (isDragging) {
            isDragging = false;
            // 拖动结束后，移除 window 上的监听器，释放资源
            window.removeEventListener('pointermove', onPointerMove);
            window.removeEventListener('pointerup', onPointerUp);
        }
    }

    // --- 调试日志 ---

    // ✅ 2. 监听 store 的变化，并在控制台打印出来
    $: {
        if ($algebraWindowVisible) {
            console.log("✅ AlgebraWindow.svelte: I see that I should be visible!");
        } else {
            // 只有在组件已经挂载后才打印 "hidden" 日志，避免初始化的干扰
            if (windowEl) {
                console.log("❌ AlgebraWindow.svelte: I see that I should be hidden.");
            }
        }
    }

    // ✅ 3. 确认组件是否被正确地挂载
    onMount(() => {
        console.log("🕵️‍♂️ AlgebraWindow.svelte: Component has mounted.");
    });
</script>

<!-- 只有在 store 的状态为 true 时才渲染窗口 -->
{#if $algebraWindowVisible}
    <div
            class="algebra-window"
            bind:this={windowEl}
            style="transform: translate({offsetX}px, {offsetY}px);"
            on:pointerdown={onPointerDown}
    >
        <div class="window-header">
            <span>代数区</span>
            <button class="close-btn" title="关闭" on:click={() => $algebraWindowVisible = false}>×</button>
        </div>
        <div class="window-content">
            <textarea
                    value={formulaText}
                    on:input={handleInput}
                    placeholder="每行一个公式, 例如:
y = x^2
y = sin(x) * 5"
            ></textarea>
        </div>
    </div>
{/if}

<style>
    .algebra-window {
        position: fixed; /* 使用 fixed 定位，使其浮动在所有内容之上 */
        top: 0;
        left: 0;
        width: 250px;
        height: 300px;
        background-color: rgba(249, 249, 249, 0.95); /* 略带透明，更有现代感 */
        border: 1px solid #ccc;
        border-radius: 6px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.2);
        display: flex;
        flex-direction: column;
        z-index: 999; /* 确保它在顶层 */
        backdrop-filter: blur(2px); /* 毛玻璃效果 */
    }

    .window-header {
        background-color: rgba(238, 238, 238, 0.8);
        padding: 8px 12px;
        cursor: grab; /* 提示用户这里可以拖动 */
        border-bottom: 1px solid #ccc;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border-top-left-radius: 6px;
        border-top-right-radius: 6px;
        user-select: none; /* 防止拖动时选中文字 */
    }
    .window-header:active {
        cursor: grabbing; /* 拖动时的手势 */
    }

    .close-btn {
        border: none;
        background: none;
        font-size: 20px;
        cursor: pointer;
        padding: 0 5px;
        line-height: 1;
    }
    .close-btn:hover {
        color: red;
    }

    .window-content {
        flex-grow: 1;
        padding: 5px;
    }

    textarea {
        width: 100%;
        height: 100%;
        border: none;
        resize: none;
        box-sizing: border-box;
        font-family: 'Fira Code', 'Courier New', Courier, monospace; /* 使用等宽字体，更适合代码/公式 */
        font-size: 14px;
        background-color: transparent;
        color: #333;
    }
    textarea:focus {
        outline: none;
    }
</style>