// src/lib/pyodide-loader.ts

import { writable } from 'svelte/store';
import { loadPyodide } from 'https://cdn.jsdelivr.net/pyodide/v0.24.1/full/pyodide.mjs';

/**
 * ✅ 核心修复 1: 为 store 的状态定义一个清晰的接口。
 * 我们明确规定 'error' 属性的类型是 'Error | null'。
 */
export interface PyodideState {
    isLoading: boolean;
    isReady: boolean;
    instance: any | null; // Pyodide 实例的类型很复杂，用 'any' 即可
    error: Error | null;
}

/**
 * ✅ 核心修复 2: 使用我们定义的接口来创建 writable store。
 * 这为 TypeScript 提供了强类型信息。
 */
export const pyodideStore = writable<PyodideState>({
    isLoading: false,
    isReady: false,
    instance: null,
    error: null,
});

// 使用一个模块级的 Promise 来确保加载过程只执行一次
let pyodidePromise: Promise<any> | null = null;

/**
 * 全局 Pyodide 加载函数。
 */
export function loadPyodideEngine() {
    if (pyodidePromise) {
        return pyodidePromise;
    }

    pyodidePromise = (async () => {
        pyodideStore.set({ isLoading: true, isReady: false, instance: null, error: null });
        console.log('开始加载 Pyodide 和 SymPy (全局仅一次)...');

        try {
            const pyodide = await loadPyodide();
            console.log('Pyodide 加载完毕，正在加载 SymPy...');
            await pyodide.loadPackage("sympy");

            pyodideStore.set({ isLoading: false, isReady: true, instance: pyodide, error: null });
            console.log('Pyodide 和 SymPy 已就绪。');

            return pyodide;
        } catch (err) {
            /**
             * ✅ 核心修复 3: 确保我们存入 store 的总是一个 Error 对象。
             * 这保证了 .message 属性的存在。
             */
            const error = err instanceof Error ? err : new Error(String(err));
            pyodideStore.set({ isLoading: false, isReady: false, instance: null, error: error });
            console.error('Pyodide 或 SymPy 加载失败:', err);
            throw err;
        }
    })();

    return pyodidePromise;
}