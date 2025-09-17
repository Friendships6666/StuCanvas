// src/lib/canvas-interaction.ts

import { get, type Writable } from 'svelte/store';

// 定义传入 stores 的类型，以获得更好的类型提示
type ViewStore = Writable<{ zoom: number; offset: { x: number; y: number; }; }>;
type CanvasSizeStore = Writable<{ width: number; height: number; }>;

/**
 * 创建一组用于 Canvas 交互的事件处理器。
 * @param view - 用于视图状态 (zoom, offset) 的 Svelte store。
 * @param canvasSize - 用于画布尺寸的 Svelte store。
 * @param devicePixelRatio - 当前设备的像素比。
 * @returns 一个包含所有事件处理器的对象。
 */
export function createCanvasInteractionHandlers(
    view: ViewStore,
    canvasSize: CanvasSizeStore,
    devicePixelRatio: number
) {
    // 内部状态，用于跟踪拖拽
    let isDragging = false;
    let lastMousePosition = { x: 0, y: 0 };

    // --- 坐标转换辅助函数 ---

    /**
     * ✅✅✅ 核心修复 ✅✅✅
     * 将 worldToScreen 函数的定义移动到这里。
     * 现在它存在于正确的 scope 中，可以被正确地导出了。
     */
    function worldToScreen(worldX: number, worldY: number) {
        const { zoom, offset } = get(view);
        const { width, height } = get(canvasSize);
        if (width === 0) return { x: -1000, y: -1000 };
        const aspectRatio = width / height;
        const viewX = (worldX - offset.x);
        const viewY = (worldY - offset.y);
        const ndcX = viewX / (aspectRatio / zoom / 2.0);
        const ndcY = viewY / (1.0 / zoom / 2.0);
        const pixelX = (ndcX + 1.0) / 2.0 * width;
        const pixelY = (1.0 - ndcY) / 2.0 * height;
        return { x: pixelX, y: pixelY };
    }

    function screenToWorld(pixelX: number, pixelY: number) {
        const { zoom, offset } = get(view);
        const { width, height } = get(canvasSize);
        if (width === 0) return { x: 0, y: 0 };
        const aspectRatio = width / height;
        const ndcX = (pixelX / width) * 2.0 - 1.0;
        const ndcY = 1.0 - (pixelY / height) * 2.0;
        const viewX = ndcX * (aspectRatio / zoom / 2.0);
        const viewY = ndcY * (1.0 / zoom / 2.0);
        const worldX = viewX + offset.x;
        const worldY = viewY + offset.y;
        return { x: worldX, y: worldY };
    }

    // --- 事件处理器 ---
    function handleMouseDown(event: MouseEvent) {
        isDragging = true;
        lastMousePosition = { x: event.clientX, y: event.clientY };
    }

    function handleMouseUp() {
        isDragging = false;
    }

    function handleMouseMove(event: MouseEvent) {
        if (!isDragging) return;

        const lastPhysicalPos = { x: lastMousePosition.x * devicePixelRatio, y: lastMousePosition.y * devicePixelRatio };
        const currentPhysicalPos = { x: event.clientX * devicePixelRatio, y: event.clientY * devicePixelRatio };

        const p0_world = screenToWorld(lastPhysicalPos.x, lastPhysicalPos.y);
        const p1_world = screenToWorld(currentPhysicalPos.x, currentPhysicalPos.y);

        view.update(v => ({
            zoom: v.zoom,
            offset: {
                x: v.offset.x - (p1_world.x - p0_world.x),
                y: v.offset.y - (p1_world.y - p0_world.y)
            }
        }));

        lastMousePosition = { x: event.clientX, y: event.clientY };
    }

    function handleWheel(event: WheelEvent) {
        event.preventDefault();
        const currentSize = get(canvasSize);
        if (currentSize.width === 0) return;

        const zoomFactor = Math.pow(1.1, -event.deltaY / 100);
        const rect = (event.target as HTMLCanvasElement).getBoundingClientRect();
        const mousePhysicalX = (event.clientX - rect.left) * devicePixelRatio;
        const mousePhysicalY = (event.clientY - rect.top) * devicePixelRatio;

        const worldPosBeforeZoom = screenToWorld(mousePhysicalX, mousePhysicalY);

        view.update(v => {
            const newZoom = v.zoom * zoomFactor;
            const aspectRatio = currentSize.width / currentSize.height;
            const ndcX = (mousePhysicalX / currentSize.width) * 2.0 - 1.0;
            const ndcY = 1.0 - (mousePhysicalY / currentSize.height) * 2.0;
            const newOffsetX = worldPosBeforeZoom.x - (ndcX * (aspectRatio / newZoom / 2.0));
            const newOffsetY = worldPosBeforeZoom.y - (ndcY * (1.0 / newZoom / 2.0));
            return { zoom: newZoom, offset: { x: newOffsetX, y: newOffsetY } };
        });
    }

    return {
        handleMouseDown,
        handleMouseUp,
        handleMouseMove,
        handleWheel,
        // 现在 worldToScreen 函数已在此作用域内定义，所以这个导出是有效的
        worldToScreen
    };
}