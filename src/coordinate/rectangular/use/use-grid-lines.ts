/*src/coordinate/rectangular/use/use-grid-lines.ts*/
import { derived, type Readable } from 'svelte/store';
import type { View } from '../../../stores/camera';
import { calculateVisibleGridLines, type LabelData, type WorldBounds } from '../grid/grid';

export function useGridLines(view: Readable<View>, aspect: Readable<number>) {
    // 整个 derived store 的结构保持不变
    return derived([view, aspect], ([$view, $aspect]) => {
        const { zoom, x: viewX, y: viewY } = $view;

        // 初始化返回值保持不变
        let minorVertices = new Float32Array(0);
        let majorVertices = new Float32Array(0);
        let axisVertices = new Float32Array(0);
        let allLabels: LabelData[] = [];

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

            // =========================================================
            // ✅✅✅ 核心修改点：几何计算逻辑大大简化 ✅✅✅
            // =========================================================
            for (const line of lines) {
                const { position: p, orientation, layer } = line;

                let p1_world, p2_world; // 线段的两个端点的“世界坐标”

                // 我们不再需要关心 lineWidth，直接生成一条理想的线
                if (orientation === 'horizontal') {
                    // 横线：从屏幕左边界延伸到右边界
                    p1_world = { x: worldBounds.left, y: p };
                    p2_world = { x: worldBounds.right, y: p };
                } else { // vertical
                    // 竖线：从屏幕下边界延伸到上边界
                    p1_world = { x: p, y: worldBounds.bottom };
                    p2_world = { x: p, y: worldBounds.top };
                }

                // 将这两个世界坐标点，转换为 NDC 坐标
                const p1_ndc = { x: (p1_world.x - viewX) * zoom / $aspect, y: (p1_world.y - viewY) * zoom };
                const p2_ndc = { x: (p2_world.x - viewX) * zoom / $aspect, y: (p2_world.y - viewY) * zoom };

                // line_verts 现在只包含 4 个数字 (2个顶点)
                const line_verts = [p1_ndc.x, p1_ndc.y, p2_ndc.x, p2_ndc.y];

                // 按类别添加到临时数组中
                if (layer === 0) temp.minor.push(...line_verts);
                else if (layer === 1) temp.major.push(...line_verts);
                else temp.axis.push(...line_verts);
            }

            // 后续逻辑保持不变
            minorVertices = new Float32Array(temp.minor);
            majorVertices = new Float32Array(temp.major);
            axisVertices = new Float32Array(temp.axis);
        }

        return { minorVertices, majorVertices, axisVertices, allLabels };
    });
}