// src/lib/webgpu/config.ts

/**
 * 点云缓冲区大小的乘数。
 * (canvas.width * canvas.height * POINT_MULTIPLIER) 决定了可以渲染的最大点数。
 * 较高的值可以处理更复杂或更密集的函数，但会消耗更多内存。
 */
export const POINT_MULTIPLIER = 50;

/**
 * 计算着色器 Pass 3 的派发宽度。
 * 这是一个性能调优参数，用于控制工作组的组织方式。
 */
export const COMPUTE_DISPATCH_WIDTH = 512;

/**
 * 画布的清除颜色（背景色）。
 */
export const CLEAR_COLOR = { r: 1.0, g: 1.0, b: 1.0, a: 1.0 };

/**
 * 主网格线的颜色。
 */
export const MAJOR_GRID_COLOR = { r: 0.8, g: 0.8, b: 0.8, a: 1.0 };

/**
 * 次网格线的颜色。
 */
export const MINOR_GRID_COLOR = { r: 0.92, g: 0.92, b: 0.92, a: 1.0 };

/**
 * X轴和Y轴的颜色。
 */
export const AXIS_COLOR = { r: 0.5, g: 0.5, b: 0.5, a: 1.0 };

/**
 * 为网格线顶点缓冲区预分配的最大顶点数。
 * 这避免了在运行时重新创建缓冲区。
 */
export const MAX_GRID_VERTICES = 8000;

/**
 * ✅ 新增: 支持的最大函数数量。
 * 这个值必须与 `shader-template.ts` 中 `ColorPalette` 结构体内的数组大小相匹配。
 * 它也决定了为颜色调色板 uniform buffer 分配多大的内存。
 */
export const MAX_FUNCTIONS = 16;