<!-- src/App.svelte -->
<script lang="ts">
    // App.svelte 的唯一职责就是加载主场景
    import RectangularCore from './coordinate/rectangular/use/rectangularCore.svelte';
</script>

<RectangularCore />

<style>
    /* 在这里定义真正的全局样式 */
    :global(html, body) {
        margin: 0;
        padding: 0;
        overflow: hidden;
        width: 100%;
        height: 100%;
        background-color: #f0f0f0; /* 设置一个背景色，以防万一 */
    }
</style>