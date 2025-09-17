// src/lib/stores.ts

import { writable, derived } from 'svelte/store';
import { getMultiFunctionShaderTemplate } from '../shader-template';
import { calculateGridLines } from './grid';
import type { LabelData, GridLineData } from './grid';


function hslToRgb(h: number, s: number, l: number): [number, number, number] {
    let r, g, b;
    if (s === 0) {
        r = g = b = l;
    } else {
        const hue2rgb = (p: number, q: number, t: number) => {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1 / 6) return p + (q - p) * 6 * t;
            if (t < 1 / 2) return q;
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
            return p;
        };
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const p = 2 * l - q;
        r = hue2rgb(p, q, h + 1 / 3);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1 / 3);
    }
    return [r, g, b];
}

export function generateRandomColor(): [number, number, number, number] {
    const hue = Math.random();
    const saturation = 0.9;
    const lightness = 0.6;
    const [r, g, b] = hslToRgb(hue, saturation, lightness);
    return [r, g, b, 1.0];
}

// --- 核心 Stores ---

export const view = writable({ zoom: 1.0, offset: { x: 0.0, y: 0.0 } });
export const canvasSize = writable({ width: 0, height: 0 });

export const functions = writable([
    { id: Date.now(), latex: 'x^2 + y^2 - 4', wgsl: 'pow(p.x, 2.0) + pow(p.y, 2.0) - 4.0', color: generateRandomColor() }
]);

/**
 * 为调试注入区创建一个专门的 store。
 */
export const debugWgslStore = writable<string>('');

// --- 派生 Stores ---

export const gridData = derived([view, canvasSize], ([$view, $canvasSize]) => {
    if ($canvasSize.width === 0) {
        return { labels: [] as LabelData[], majorVertices: new Float32Array(), minorVertices: new Float32Array(), axisVertices: new Float32Array() };
    }
    return calculateGridLines($view.zoom, $view.offset, $canvasSize.width / $canvasSize.height);
});

/**
 * 派生出一个统一的 WGSL 着色器代码。
 * 它会智能地将来自 MathLive 的函数和来自调试区的函数合并在一起。
 */
export const combinedWgslShader = derived(
    [functions, debugWgslStore],
    ([$functions, $debugWgslStore]) => {
        const mathliveBodies = $functions.map(f => f.wgsl || '1.0');

        const debugBody = $debugWgslStore.trim();
        const allBodies = debugBody ? [...mathliveBodies, debugBody] : mathliveBodies;

        if (allBodies.length === 0) {
            // 提供一个默认的、不会渲染任何东西的着色器体，以防止错误
            return getMultiFunctionShaderTemplate(['1.0']);
        }

        return getMultiFunctionShaderTemplate(allBodies);
    }
);