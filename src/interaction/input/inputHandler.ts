// src/interaction/input/inputHandler.ts

import {pan , zoomAtPoint} from '../../stores/camera';
import type { Renderer } from '../../renderCore/renderer';

export interface InputHandler {
    destroy : () => void;
}

/**
 * 初始化所有用户输入事件的监听器。
 * @param canvas - 交互发生的HTMLCanvasElement
 * @param renderer - 渲染器实例，用于读取调试值
 * @param getAspect - 一个返回当前宽高比的函数
 * @param requestRender - 一个请求重新渲染的函数
 * @param onDebugValue - 一个接收调试值的回调函数
 * @returns 一个包含destroy方法的对象，用于清理事件监听器
 */
export function initializeInputHandlers (
    canvas : HTMLCanvasElement,
    renderer: Renderer, // ✅ 接收 renderer
    getAspect : () => number,
    requestRender : () => void,
    onDebugValue: (value: number) => void // ✅ 接收回调
) : InputHandler {
    let isDragging = false;
    let lastMousePos = { x : 0, y : 0 };
    let readbackTimeout: number | null = null;

    function handleMouseDown ( event : MouseEvent ) {
        isDragging = true;
        lastMousePos = { x : event.clientX, y : event.clientY };
        canvas.style.cursor = 'grabbing';
    }

    function handleMouseUp () {
        isDragging = false;
        canvas.style.cursor = 'grab';
    }

    function handleMouseMove ( event : MouseEvent ) {
        // --- 逻辑1: 处理拖拽平移 ---
        if (isDragging) {
            const dx = event.clientX - lastMousePos.x;
            const dy = event.clientY - lastMousePos.y;
            lastMousePos = { x : event.clientX, y : event.clientY };
            pan ( dx , dy , canvas.clientWidth , canvas.clientHeight , getAspect () );
            requestRender ();
            return; // 如果在拖拽，则不执行后续的调试逻辑
        }

        // --- 逻辑2: 处理调试值读取 (节流) ---
        if (readbackTimeout) return;

        readbackTimeout = window.setTimeout(async () => {
            const rect = canvas.getBoundingClientRect();
            const mouseX = Math.floor(event.clientX - rect.left);
            const mouseY = Math.floor(event.clientY - rect.top);

            if (mouseX >= 0 && mouseX < canvas.width && mouseY >= 0 && mouseY < canvas.height) {
                try {
                    const value = await renderer.readDebugValueAt(mouseX, mouseY);
                    onDebugValue(value); // ✅ 通过回调传出值
                } catch (e) {
                    console.error("Failed to read debug value:", e);
                }
            }
            readbackTimeout = null;
        }, 50);
    }

    function handleWheel ( event : WheelEvent ) {
        event.preventDefault ();
        const rect = canvas.getBoundingClientRect ();
        const mouseX = event.clientX - rect.left;
        const mouseY = event.clientY - rect.top;
        zoomAtPoint ( event.deltaY , mouseX , mouseY , canvas.clientWidth , canvas.clientHeight , getAspect () );
        requestRender ();
    }

    canvas.addEventListener ( 'wheel' , handleWheel );
    canvas.addEventListener ( 'mousedown' , handleMouseDown );
    window.addEventListener ( 'mouseup' , handleMouseUp );
    // ✅ *** 核心修复 ***
    // 现在这个 mousemove 监听器同时处理拖拽和调试
    window.addEventListener ( 'mousemove' , handleMouseMove );

    const destroy = () => {
        canvas.removeEventListener ( 'wheel' , handleWheel );
        canvas.removeEventListener ( 'mousedown' , handleMouseDown );
        window.removeEventListener ( 'mouseup' , handleMouseUp );
        window.removeEventListener ( 'mousemove' , handleMouseMove );
        if (readbackTimeout) clearTimeout(readbackTimeout);
    };

    return { destroy };
}