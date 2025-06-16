// src/coordinate/rectangular/interaction/inputHandler.ts
import { pan, zoomAtPoint } from '../../stores/camera';

// 定义一个返回类型，方便管理
export interface InputHandler {
    destroy: () => void;
}

/**
 * 初始化所有用户输入事件的监听器。
 * @param canvas - 交互发生的HTMLCanvasElement
 * @param getAspect - 一个返回当前宽高比的函数
 * @param requestRender - 一个请求重新渲染的函数
 * @returns 一个包含destroy方法的对象，用于清理事件监听器
 */
export function initializeInputHandlers(
    canvas: HTMLCanvasElement,
    getAspect: () => number,
    requestRender: () => void
): InputHandler {
    let isDragging = false;
    let lastMousePos = { x: 0, y: 0 };

    function handleMouseDown(event: MouseEvent) {
        isDragging = true;
        lastMousePos = { x: event.clientX, y: event.clientY };
        canvas.style.cursor = 'grabbing';
    }

    function handleMouseUp() {
        isDragging = false;
        canvas.style.cursor = 'grab';
    }

    function handleMouseMove(event: MouseEvent) {
        if (!isDragging) return;
        const dx = event.clientX - lastMousePos.x;
        const dy = event.clientY - lastMousePos.y;
        lastMousePos = { x: event.clientX, y: event.clientY };
        pan(dx, dy, canvas.clientWidth, canvas.clientHeight, getAspect());
        requestRender();
    }

    function handleWheel(event: WheelEvent) {
        event.preventDefault();
        const rect = canvas.getBoundingClientRect();
        const mouseX = event.clientX - rect.left;
        const mouseY = event.clientY - rect.top;
        zoomAtPoint(event.deltaY, mouseX, mouseY, canvas.clientWidth, canvas.clientHeight, getAspect());
        requestRender();
    }

    canvas.addEventListener('wheel', handleWheel);
    canvas.addEventListener('mousedown', handleMouseDown);
    window.addEventListener('mouseup', handleMouseUp);
    window.addEventListener('mousemove', handleMouseMove);

    const destroy = () => {
        canvas.removeEventListener('wheel', handleWheel);
        canvas.removeEventListener('mousedown', handleMouseDown);
        window.removeEventListener('mouseup', handleMouseUp);
        window.removeEventListener('mousemove', handleMouseMove);
    };

    return { destroy };
}