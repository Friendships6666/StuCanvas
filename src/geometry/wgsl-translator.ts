export function translateJsExpressionToWgsl(jsExpr: string): string {
    let wgslExpr = jsExpr.trim();

    // 1. 确保所有数值字面量都是浮点数
    wgslExpr = wgslExpr.replace(/(?<![\w.])(\d+)(?![.\w])/g, '$1.0');

    // 2. 替换数学常数
    wgslExpr = wgslExpr.replace(/\bpi\b/g, '3.1415926535');

    // 3. 替换特定的对数函数
    wgslExpr = wgslExpr.replace(/\blog10\s*\(([^)]+)\)/g, '(log($1)/log(10.0))');
    wgslExpr = wgslExpr.replace(/\blog2\s*\(([^)]+)\)/g, '(log($1)/log(2.0))');
    wgslExpr = wgslExpr.replace(/log\(([^,]+?),\s*([^)]+?)\)/g, '(log($1) / log($2))');

    // ✅ *** FIX: The regular expression was too restrictive and failed to match simple numeric exponents. ***
    // This new regex correctly captures identifiers, literals, and parenthesized groups.
    const powerRegex = /([a-zA-Z_][a-zA-Z0-9_.]*|\d+(?:\.\d*)?|\([^)]+\))\s*\^\s*([a-zA-Z_][a-zA-Z0-9_.]*|\d+(?:\.\d*)?|\([^)]+\))/g;

    wgslExpr = wgslExpr.replace(powerRegex, (match, base, exponentStr) => {
        const exponentNum = parseFloat(exponentStr);

        // Check if the exponent is a known, finite integer
        if (Number.isFinite(exponentNum) && Number.isInteger(exponentNum)) {
            const intExp = Math.round(exponentNum);

            if (intExp % 2 !== 0) {
                // Odd integer exponent: preserve the sign of the base.
                return `(sign(${base}) * pow(abs(${base}), ${exponentStr}))`;
            } else {
                // Even integer exponent: result is always positive.
                return `pow(abs(${base}), ${exponentStr})`;
            }
        }

        // For non-integer or variable exponents, use the standard pow function.
        return `pow(${base}, ${exponentStr})`;
    });

    return `(${wgslExpr})`;
}