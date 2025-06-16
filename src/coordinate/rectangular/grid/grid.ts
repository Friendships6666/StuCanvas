import { formatNumber } from '../label/formatter';

/**
 * 定义单条网格线的数据结构。
 */
export type LineData = {
    id: string;
    position: number;
    orientation: 'horizontal' | 'vertical';
    color: string;
    lineWidth: number;
    layer: number;
};

/**
 * 定义单个标签的数据结构。
 */
export type LabelData = {
    id: string;
    text: string;
    worldX: number;
    worldY: number;
    orientation: 'horizontal' | 'vertical' | 'origin';
};

/**
 * 定义世界坐标边界的接口。
 */
export interface WorldBounds {
    left: number;
    right: number;
    top: number;
    bottom: number;
}

/**
 * 根据当前的缩放级别，计算出一个“优美”的网格间距。
 */
function getNiceGridSpacing(zoom: number): number {
    // 我们期望屏幕上大约有 8 条主网格线，基于此来计算理想间距
    const idealSpacing = 2 / zoom / 8;
    const powerOf10 = 10 ** Math.floor(Math.log10(idealSpacing));
    const relativeSpacing = idealSpacing / powerOf10;

    if (relativeSpacing < 1.5) return 1 * powerOf10;
    if (relativeSpacing < 3.5) return 2 * powerOf10;
    if (relativeSpacing < 7.5) return 5 * powerOf10;
    return 10 * powerOf10;
}

/**
 * 计算在给定视口边界内所有可见的网格线和标签。
 * @param zoom - 当前的缩放级别，用于计算网格密度
 * @param bounds - 预先计算好的世界坐标边界
 * @returns 一个包含所有可见线条和标签数组的对象
 */
export function calculateVisibleGridLines(
    zoom: number,
    bounds: WorldBounds
): { lines: LineData[], labels: LabelData[] } {
    const majorGridSpacing = getNiceGridSpacing(zoom);
    const minorGridSpacing = majorGridSpacing * 0.2;

    const lines: LineData[] = [];
    const labels: LabelData[] = [];

    // --- 使用整数索引进行所有逻辑判断，避免浮点数精度问题 ---

    // Layer 0: 次网格线 (最底层)
    const i_start_minor_x = Math.ceil(bounds.left / minorGridSpacing);
    const i_end_minor_x = Math.floor(bounds.right / minorGridSpacing);
    for (let i = i_start_minor_x; i <= i_end_minor_x; i++) {
        if (i % 5 !== 0) {
            const x = i * minorGridSpacing;
            lines.push({ id: `minor_v_${i}`, position: x, orientation: 'vertical', color: '#e0e0e0', lineWidth: 0.001, layer: 0 });
        }
    }
    const i_start_minor_y = Math.ceil(bounds.bottom / minorGridSpacing);
    const i_end_minor_y = Math.floor(bounds.top / minorGridSpacing);
    for (let i = i_start_minor_y; i <= i_end_minor_y; i++) {
        if (i % 5 !== 0) {
            const y = i * minorGridSpacing;
            lines.push({ id: `minor_h_${i}`, position: y, orientation: 'horizontal', color: '#e0e0e0', lineWidth: 0.001, layer: 0 });
        }
    }

    // Layer 1: 主网格线和标签
    const i_start_major_x = Math.ceil(bounds.left / majorGridSpacing);
    const i_end_major_x = Math.floor(bounds.right / majorGridSpacing);
    for (let i = i_start_major_x; i <= i_end_major_x; i++) {
        if (i !== 0) {
            const x = i * majorGridSpacing;
            lines.push({ id: `major_v_${i}`, position: x, orientation: 'vertical', color: '#cccccc', lineWidth: 0.002, layer: 1 });
            labels.push({ id: `label_v_${i}`, text: formatNumber(x), worldX: x, worldY: 0, orientation: 'vertical' });
        }
    }
    const i_start_major_y = Math.ceil(bounds.bottom / majorGridSpacing);
    const i_end_major_y = Math.floor(bounds.top / majorGridSpacing);
    for (let i = i_start_major_y; i <= i_end_major_y; i++) {
        if (i !== 0) {
            const y = i * majorGridSpacing;
            lines.push({ id: `major_h_${i}`, position: y, orientation: 'horizontal', color: '#cccccc', lineWidth: 0.002, layer: 1 });
            labels.push({ id: `label_h_${i}`, text: formatNumber(y), worldX: 0, worldY: y, orientation: 'horizontal' });
        }
    }

    // Layer 2: 主轴和原点标签
    lines.push({ id: 'axis_x', position: 0, orientation: 'horizontal', color: '#000000', lineWidth: 0.005, layer: 2 });
    lines.push({ id: 'axis_y', position: 0, orientation: 'vertical', color: '#000000', lineWidth: 0.005, layer: 2 });
    labels.push({ id: 'label_origin', text: '0', worldX: 0, worldY: 0, orientation: 'origin' });

    return { lines, labels };
}