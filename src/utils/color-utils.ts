// src/lib/color-utils.ts

/**
 * 将CSS十六进制颜色字符串 (e.g., '#ff0000') 转换为
 * WebGPU着色器可以使用的Float32Array([r, g, b, a])格式。
 * @param hex - 十六进制颜色字符串
 * @returns 一个包含rgba值的Float32Array
 */
export function hexToVec4(hex: string): Float32Array {
    // 确保处理3位和6位的十六进制颜色
    const fullHex = hex.length === 4
        ? `#${hex[1]}${hex[1]}${hex[2]}${hex[2]}${hex[3]}${hex[3]}`
        : hex;

    const r = parseInt(fullHex.slice(1, 3), 16) / 255;
    const g = parseInt(fullHex.slice(3, 5), 16) / 255;
    const b = parseInt(fullHex.slice(5, 7), 16) / 255;
    return new Float32Array([r, g, b, 1.0]);
}

// 未来可以添加更多工具函数，比如数学计算等...