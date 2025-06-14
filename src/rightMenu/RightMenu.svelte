<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { rightMenu, algebraWindowVisible } from '../stores/ui';

    function handleClickOutside() {
        rightMenu.set({ visible: false, x: 0, y: 0 });
    }

    function handleCheckboxChange() {
        algebraWindowVisible.update(visible => !visible);
    }

    onMount(() => {
        // 这个监听器现在只会处理菜单外部的点击
        window.addEventListener('click', handleClickOutside);
    });

    onDestroy(() => {
        window.removeEventListener('click', handleClickOutside);
    });
</script>

{#if $rightMenu.visible}
    <!-- ✅ THE DEFINITIVE FIX: 添加 on:click|stopPropagation 事件修饰符 -->
    <!-- 这会阻止菜单内部的点击事件冒泡到 window，从而避免菜单被立即关闭 -->
    <div
            class="right-menu"
            style="left: {$rightMenu.x}px; top: {$rightMenu.y}px;"
            on:click|stopPropagation
    >
        <ul>
            <li>
                <label>
                    <input
                            type="checkbox"
                            checked={$algebraWindowVisible}
                            on:change={handleCheckboxChange}
                    />
                    代数绘图区
                </label>
            </li>
        </ul>
    </div>
{/if}

<style>
    .right-menu {
        position: fixed;
        background-color: #ffffff;
        border: 1px solid #d1d1d1;
        box-shadow: 0 4px 8px rgba(0,0,0,0.1);
        border-radius: 6px;
        padding: 5px 0;
        z-index: 1000;
        min-width: 180px;
        font-family: sans-serif;
    }
    ul {
        list-style: none;
        margin: 0;
        padding: 0;
    }
    li label {
        display: flex;
        align-items: center;
        padding: 8px 15px;
        cursor: pointer;
        font-size: 14px;
        color: #333;
    }
    li label:hover {
        background-color: #f0f0f0;
    }
    input[type="checkbox"] {
        margin-right: 10px;
        width: 16px;
        height: 16px;
    }
</style>