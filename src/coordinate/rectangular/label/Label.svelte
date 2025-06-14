<script lang="ts">
    import { view } from '../../../stores/camera';

    export let text: string;
    export let worldX: number;
    export let worldY: number;
    export let orientation: 'horizontal' | 'vertical' | 'origin';
    export let aspect: number;

    let top: string;
    let left: string;
    let visible = true;

    $: {
        const view_pos_x = (worldX - $view.x) * $view.zoom;
        const view_pos_y = (worldY - $view.y) * $view.zoom;
        const clip_pos_x = view_pos_x / aspect;
        const clip_pos_y = view_pos_y;

        // 扩大一点边界，防止标签在边缘突然消失
        visible = Math.abs(clip_pos_x) <= 1.1 && Math.abs(clip_pos_y) <= 1.1;

        const screen_percent_x = (clip_pos_x + 1) / 2 * 100;
        const screen_percent_y = (-clip_pos_y + 1) / 2 * 100;

        left = `${screen_percent_x}%`;
        top = `${screen_percent_y}%`;
    }
</script>

<!--
  HTML 结构保持不变，但现在不再需要内部的 span 来做特殊处理了，
  因为父 div 的 transform 会一步到位。
-->
{#if visible}
    <div
            class="grid-label"
            class:horizontal={orientation === 'horizontal'}
            class:vertical={orientation === 'vertical'}
            class:origin={orientation === 'origin'}
            style="top: {top}; left: {left};"
    >
        {text}
    </div>
{/if}

<style>
    .grid-label {
        position: absolute;
        color: #666;
        background-color: transparent;
        font-family: Arial, sans-serif;
        font-size: 13px;
        /* text-shadow 让文字在网格线上更清晰 */
        text-shadow: 1px 1px 0 #fff, -1px 1px 0 #fff, 1px -1px 0 #fff, -1px -1px 0 #fff;
        white-space: nowrap;
        pointer-events: none;
        user-select: none;
    }

    /* --- ✅ 最终修复版样式 --- */

    /**
     * Y轴标签 (对应水平线)
     * 目标：标签的右侧紧贴Y轴，并垂直居中于网格线。
     * translate(-100%, -50%) 表示：向左移动自身100%宽度，向上移动自身50%高度。
     * -5px 是额外的间距。
     */
    .grid-label.horizontal {
        transform: translate(calc(-100% - 5px), -50%);
    }

    /**
     * X轴标签 (对应垂直线)
     * 目标：标签的顶部紧贴X轴，并水平居中于网格线。
     * translate(-50%, 8px) 表示：向左移动自身50%宽度，向下移动8px。
     * 8px 是标签与X轴之间的间距。
     */
    .grid-label.vertical {
        transform: translate(-50%, 8px);
    }

    /**
     * 原点 "0" 标签
     * 目标：标签的右上角对齐原点。
     */
    .grid-label.origin {
        /* 向左移动100%宽度，再向左偏移4px；向下移动4px */
        transform: translate(calc(-100% - 4px), 4px);
    }
</style>