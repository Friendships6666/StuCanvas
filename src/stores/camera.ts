// src/stores/camera.ts (修改后)
import { writable } from 'svelte/store';

// ✅ 1. 定义并导出 View 类型
export interface View {
    x: number;
    y: number;
    zoom: number;
}

// 2. 使用这个类型来注解 store
export const view = writable<View>({ x: 0, y: 0, zoom: 0.2 });

/**
 * 平移相机
 */
export function pan(dx: number, dy: number, canvasWidth: number, canvasHeight: number, aspect: number) {
    view.update(v => ({
        ...v,
        x: v.x - (dx / canvasWidth) * (2 / v.zoom) * aspect,
        y: v.y + (dy / canvasHeight) * (2 / v.zoom),
    }));
}

/**
 * 在指定点进行缩放
 */
export function zoomAtPoint(wheelDelta: number, mouseX: number, mouseY: number, canvasWidth: number, canvasHeight: number, aspect: number) {
    const zoomSpeed = 0.001;

    view.update(currentView => {
        const clipX = (mouseX / canvasWidth) * 2 - 1;
        const clipY = (mouseY / canvasHeight) * -2 + 1;
        const viewX = clipX * aspect;
        const viewY = clipY;
        const worldX_beforeZoom = (viewX / currentView.zoom) + currentView.x;
        const worldY_beforeZoom = (viewY / currentView.zoom) + currentView.y;

        const newZoom = currentView.zoom * (1 - wheelDelta * zoomSpeed);

        const worldX_afterZoom = (viewX / newZoom) + currentView.x;
        const worldY_afterZoom = (viewY / newZoom) + currentView.y;

        const dx = worldX_afterZoom - worldX_beforeZoom;
        const dy = worldY_afterZoom - worldY_beforeZoom;

        return {
            x: currentView.x - dx,
            y: currentView.y - dy,
            zoom: newZoom,
        };
    });
}