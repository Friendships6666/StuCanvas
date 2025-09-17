// src/lib/grid.ts

export interface LabelData {
    id: string;
    text: string;
    worldX: number;
    worldY: number;
    // ✅ 增加了 'origin' 类型以适配新的CSS样式和逻辑
    orientation: 'horizontal' | 'vertical' | 'origin';
}

export interface GridLineData {
    majorVertices: Float32Array;
    minorVertices: Float32Array;
    axisVertices: Float32Array;
    labels: LabelData[];
}

function formatNumber(n: number): string {
    if (Math.abs(n) < 0.001 && n !== 0) return n.toExponential(1);
    const fixed = parseFloat(n.toPrecision(10));
    return String(fixed);
}

function getNiceGridSpacing(zoom: number): number {
    const idealSpacing = 2.0 / zoom / 8;
    const powerOf10 = 10 ** Math.floor(Math.log10(idealSpacing));
    const relativeSpacing = idealSpacing / powerOf10;

    if (relativeSpacing < 1.5) return 1 * powerOf10;
    if (relativeSpacing < 3.5) return 2 * powerOf10;
    if (relativeSpacing < 7.5) return 5 * powerOf10;
    return 10 * powerOf10;
}

export function calculateGridLines(
    zoom: number,
    offset: { x: number, y: number },
    aspectRatio: number
): GridLineData {
    // 这里的 worldBounds 计算保持不变，因为它正确地定义了需要生成线条的范围
    const world_x_radius = aspectRatio / (2.0 * zoom);
    const world_y_radius = 1.0 / (2.0 * zoom);
    const worldBounds = {
        left: offset.x - world_x_radius, right: offset.x + world_x_radius,
        top: offset.y + world_y_radius, bottom: offset.y - world_y_radius,
    };

    const majorGridSpacing = getNiceGridSpacing(zoom);
    const minorGridSpacing = majorGridSpacing / 5.0;

    const majorVerts: number[] = [];
    const minorVerts: number[] = [];
    const axisVerts: number[] = [];
    const labels: LabelData[] = [];

    // --- 垂直线 (对应 X 轴标签) ---
    const i_start_major_x = Math.ceil(worldBounds.left / majorGridSpacing);
    const i_end_major_x = Math.floor(worldBounds.right / majorGridSpacing);
    for (let i = i_start_major_x; i <= i_end_major_x; i++) {
        if (i === 0) continue; // 坐标轴单独处理
        const x = i * majorGridSpacing;
        majorVerts.push(x, worldBounds.bottom, x, worldBounds.top);

        // ✅ 核心修正: 标签的 worldY 固定在 X 轴上 (y=0)
        labels.push({
            id: `label_v_${i}`,
            text: formatNumber(x),
            worldX: x,
            worldY: 0,
            orientation: 'vertical',
        });
    }
    const i_start_minor_x = Math.ceil(worldBounds.left / minorGridSpacing);
    const i_end_minor_x = Math.floor(worldBounds.right / minorGridSpacing);
    for (let i = i_start_minor_x; i <= i_end_minor_x; i++) {
        if (i % 5 === 0) continue;
        const x = i * minorGridSpacing;
        minorVerts.push(x, worldBounds.bottom, x, worldBounds.top);
    }

    // --- 水平线 (对应 Y 轴标签) ---
    const i_start_major_y = Math.ceil(worldBounds.bottom / majorGridSpacing);
    const i_end_major_y = Math.floor(worldBounds.top / majorGridSpacing);
    for (let i = i_start_major_y; i <= i_end_major_y; i++) {
        if (i === 0) continue; // 坐标轴单独处理
        const y = i * majorGridSpacing;
        majorVerts.push(worldBounds.left, y, worldBounds.right, y);

        // ✅ 核心修正: 标签的 worldX 固定在 Y 轴上 (x=0)
        labels.push({
            id: `label_h_${i}`,
            text: formatNumber(y),
            worldX: 0,
            worldY: y,
            orientation: 'horizontal',
        });
    }
    const i_start_minor_y = Math.ceil(worldBounds.bottom / minorGridSpacing);
    const i_end_minor_y = Math.floor(worldBounds.top / minorGridSpacing);
    for (let i = i_start_minor_y; i <= i_end_minor_y; i++) {
        if (i % 5 === 0) continue;
        const y = i * minorGridSpacing;
        minorVerts.push(worldBounds.left, y, worldBounds.right, y);
    }

    // --- 坐标轴和原点标签 ---
    if (worldBounds.left < 0 && worldBounds.right > 0) {
        axisVerts.push(0, worldBounds.bottom, 0, worldBounds.top); // Y 轴
    }
    if (worldBounds.bottom < 0 && worldBounds.top > 0) {
        axisVerts.push(worldBounds.left, 0, worldBounds.right, 0); // X 轴
    }

    // ✅ 核心修正: 单独添加原点 "0" 的标签
    labels.push({
        id: 'label_origin',
        text: '0',
        worldX: 0,
        worldY: 0,
        orientation: 'origin'
    });

    return {
        majorVertices: new Float32Array(majorVerts),
        minorVertices: new Float32Array(minorVerts),
        axisVertices: new Float32Array(axisVerts),
        labels,
    };
}