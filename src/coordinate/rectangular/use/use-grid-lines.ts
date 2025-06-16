/*src/coordinate/rectangular/use/use-grid-lines.ts*/
import { derived, type Readable } from 'svelte/store';
import type { View } from '../../../stores/camera';
import { calculateVisibleGridLines, type LabelData, type WorldBounds } from '../grid/grid';

/**
 * 一个 Svelte "hook"，用于根据相机视图和画布宽高比计算网格线和标签。
 * @param view - 包含相机位置 (x, y) 和缩放 (zoom) 的 store。
 * @param aspect - 画布的宽高比 (width / height) 的 store。
 * @returns 一个 derived store，其值包含用于渲染的顶点数组和标签数据。
 */
export function useGridLines(view: Readable<View>, aspect: Readable<number>) {
    return derived([view, aspect], ([$view, $aspect]) => {
        const { zoom, x: viewX, y: viewY } = $view;

        // 初始化返回值
        let minorVertices = new Float32Array(0);
        let majorVertices = new Float32Array(0);
        let axisVertices = new Float32Array(0);
        let allLabels: LabelData[] = [];

        // ✅ 修正: 移除对 'canvas' 的引用。
        // 只要 $aspect 是一个有效的正数，就意味着 canvas 的尺寸是有效的。
        if ($aspect > 0) {
            const world_x_radius = $aspect / zoom;
            const world_y_radius = 1.0 / zoom;
            const worldBounds: WorldBounds = {
                left: viewX - world_x_radius, right: viewX + world_x_radius,
                top: viewY + world_y_radius, bottom: viewY - world_y_radius,
            };

            const { lines, labels } = calculateVisibleGridLines(zoom, worldBounds);
            allLabels = labels;

            const temp: { minor: number[], major: number[], axis: number[] } = { minor: [], major: [], axis: [] };

            for (const line of lines) {
                const { position: p, orientation, lineWidth, layer } = line;
                const widthScale = (layer === 2) ? 0.75 : 1.0;
                const half_width = (lineWidth * widthScale / 2) / zoom;
                let p1, p2, p3, p4;

                if (orientation === 'horizontal') {
                    p1 = { x: worldBounds.left, y: p - half_width }; p2 = { x: worldBounds.right, y: p - half_width };
                    p3 = { x: worldBounds.right, y: p + half_width }; p4 = { x: worldBounds.left, y: p + half_width };
                } else {
                    p1 = { x: p - half_width, y: worldBounds.bottom }; p2 = { x: p + half_width, y: worldBounds.bottom };
                    p3 = { x: p + half_width, y: worldBounds.top }; p4 = { x: p - half_width, y: worldBounds.top };
                }

                const c1 = { x: (p1.x - viewX) * zoom / $aspect, y: (p1.y - viewY) * zoom };
                const c2 = { x: (p2.x - viewX) * zoom / $aspect, y: (p2.y - viewY) * zoom };
                const c3 = { x: (p3.x - viewX) * zoom / $aspect, y: (p3.y - viewY) * zoom };
                const c4 = { x: (p4.x - viewX) * zoom / $aspect, y: (p4.y - viewY) * zoom };
                const rect_verts = [c1.x, c1.y, c2.x, c2.y, c3.x, c3.y, c1.x, c1.y, c3.x, c3.y, c4.x, c4.y];

                if (layer === 0) temp.minor.push(...rect_verts);
                else if (layer === 1) temp.major.push(...rect_verts);
                else temp.axis.push(...rect_verts);
            }

            minorVertices = new Float32Array(temp.minor);
            majorVertices = new Float32Array(temp.major);
            axisVertices = new Float32Array(temp.axis);
        }

        return { minorVertices, majorVertices, axisVertices, allLabels };
    });
}