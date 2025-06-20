// src/coordinate/rectangular/label/formatter.ts

/**
 * 将数字格式化为字符串。
 * - 长度大于5位的数字将使用科学计数法。
 * - 科学计数法中的 ".0e" 将被简化为 "e"。
 * - 在格式化前，对数字进行高精度舍入以消除浮点数噪声。
 */
export function formatNumber(num: number): string {
    // --- FIX: 移除这个错误的零值判断 ---
    // if (Math.abs(num) < 1e-9) {
    //     return '0';
    // }

    // 1. 清理浮点数噪声
    const cleanedNum = parseFloat(num.toPrecision(14));

    // 2. 使用清理后的数字进行后续所有判断和转换
    // 检查数字的绝对值是否大于等于100,000 或 小于等于 0.0001
    if (Math.abs(cleanedNum) >= 100000 || (Math.abs(cleanedNum) > 0 && Math.abs(cleanedNum) <= 0.0001)) {
        const exponentialStr = cleanedNum.toExponential();
        return exponentialStr.replace('.0e', 'e').toString();
    }

    return cleanedNum.toString();
}