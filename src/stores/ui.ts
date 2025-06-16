import { writable } from 'svelte/store';

export interface FormulaDomain {
    id: string;
    rawString: string;
}

// ✅ 核心改变：移除了 color 属性
export interface FormulaEntry {
    id: string;
    expression: string;
    domains: FormulaDomain[];
    enabled: boolean;
}

export const formulas = writable<FormulaEntry[]>([]);
export const algebraWindowVisible = writable(true);
export const rightMenu = writable({ visible: false, x: 0, y: 0 });