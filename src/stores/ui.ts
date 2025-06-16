/*src/stores/ui.ts*/
import { writable } from 'svelte/store';

/**
 * 控制右键菜单的显示状态和位置
 */
export const rightMenu = writable({
    visible: false,
    x: 0,
    y: 0
});

/**
 * 控制代数窗口是否显示
 */
export const algebraWindowVisible = writable(false);

/**
 * 存储用户输入的公式字符串数组。
 * 我们提供两个默认公式，以便用户初次打开时能立即看到效果。
 */
export const formulas = writable<string[]>([
    'y = x^2',
    'y = sin(x) * 3'
]);