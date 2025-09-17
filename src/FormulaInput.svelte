<script lang="ts">
    import { onMount, createEventDispatcher } from 'svelte';
    import { MathfieldElement } from 'mathlive';
    import "https://esm.run/@cortex-js/compute-engine";

    // 从全局加载器导入 store，不再自己管理加载过程
    import { pyodideStore } from './lib/pyodide-loader';

    // --- 1. Props 和 Events ---
    export let value: string = '';
    const dispatch = createEventDispatcher();

    // --- 2. 状态管理 ---
    // 组件的状态现在直接来源于全局 store
    $: isEngineReady = $pyodideStore.isReady;
    $: isLoadingEngine = $pyodideStore.isLoading;
    $: globalError = $pyodideStore.error ? `引擎加载失败: ${$pyodideStore.error.message}` : null;

    let mathfieldElement: MathfieldElement;

    // 用于UI反馈的验证状态
    type ValidationStatus = 'idle' | 'pending' | 'valid' | 'invalid';
    let validationStatus: ValidationStatus = 'idle';

    // --- 3. 防抖 (Debouncing) 工具 ---
    let debounceTimer: number;
    function debounce(func: Function, delay: number) {
        clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => func(), delay);
    }

    // --- 4. 核心转换逻辑: MathJSON -> SymPy String (完全保留) ---
    function mathJsonToSympyString(expr: any): string {
        if (typeof expr === 'string') return expr;
        if (typeof expr === 'number') return expr.toString();
        if (Array.isArray(expr)) {
            const [operator, ...args] = expr;
            const processedArgs = args.map(arg => mathJsonToSympyString(arg));
            const op = operator.toLowerCase();
            switch (op) {
                case 'add': {
                    if (processedArgs.length === 0) return '0';
                    let result = processedArgs[0];
                    for (let i = 1; i < processedArgs.length; i++) {
                        const term = processedArgs[i];
                        if (term.startsWith('-')) result += ` - ${term.substring(1)}`;
                        else result += ` + ${term}`;
                    }
                    return `(${result})`;
                }
                case 'subtract': return `(${processedArgs[0]} - ${processedArgs[1]})`;
                case 'multiply': return `(${processedArgs.join(' * ')})`;
                case 'divide':   return `(${processedArgs[0]} / ${processedArgs[1]})`;
                case 'power':    return `(${processedArgs[0]}**${processedArgs[1]})`;
                case 'negate':   return `(-${processedArgs[0]})`;
                case 'sqrt':     return `sqrt(${processedArgs[0]})`;
                case 'equal':    return `Eq(${processedArgs[0]}, ${processedArgs[1]})`;
                case 'rational': return `Rational(${processedArgs[0]}, ${processedArgs[1]})`;
                case 'ln':       return `log(${processedArgs[0]})`;
                case 'log':
                    return processedArgs.length > 1
                        ? `log(${processedArgs[0]}, ${processedArgs[1]})`
                        : `log(${processedArgs[0]})`;
                case 'sin': case 'cos': case 'tan': case 'sec': case 'csc': case 'cot':
                    return `${op}(${processedArgs.join(', ')})`;
                case 'arcsin':   return `asin(${processedArgs[0]})`;
                case 'arccos':   return `acos(${processedArgs[0]})`;
                case 'arctan':   return `atan(${processedArgs[0]})`;
                case 'arcsec':   return `asec(${processedArgs[0]})`;
                case 'arccsc':   return `acsc(${processedArgs[0]})`;
                case 'arccot':   return `acot(${processedArgs[0]})`;
                case 'sinh': case 'cosh': case 'tanh': case 'sech': case 'csch': case 'coth':
                    return `${op}(${processedArgs.join(', ')})`;
                case 'arsinh':   return `asinh(${processedArgs[0]})`;
                case 'arccosh':  return `acosh(${processedArgs[0]})`;
                case 'artanh':   return `atanh(${processedArgs[0]})`;
                case 'arsech':   return `asech(${processedArgs[0]})`;
                case 'arcsch':   return `acsch(${processedArgs[0]})`;
                case 'arcoth':   return `acoth(${processedArgs[0]})`;
                default:         return `${operator}(${processedArgs.join(', ')})`;
            }
        }
        throw new Error(`未知的 MathJSON 表达式类型: ${JSON.stringify(expr)}`);
    }

    // --- 5. Python 化简逻辑 (完全保留) ---
    async function simplifyWithPyodide(pyodide: any, expression: string): Promise<string> {
        pyodide.globals.set('js_expr', expression);
        const pythonScript = `
from sympy import sympify, fraction, cancel, horner, sin, cos, tan, sec, csc, cot, log, exp, Symbol, Dummy, Function, Wild, solve, E
# --- 主处理流程 ---
# 1. 初始解析和符号识别
original_expr = sympify(js_expr)
symbols = sorted(list(original_expr.free_symbols), key=lambda s: s.name)
x = next((s for s in symbols if s.name == 'x'), symbols[0] if symbols else None)
# 2. 通用三角函数替换
u = Wild('u')
substituted_expr = original_expr.replace(tan(u), sin(u)/cos(u)) \\
                                  .replace(sec(u), 1/cos(u)) \\
                                  .replace(csc(u), 1/sin(u)) \\
                                  .replace(cot(u), cos(u)/sin(u))
# 3. 通分并提取分子
numerator = fraction(cancel(substituted_expr))[0]
# 4. 数值稳定性变换 (ln -> exp)
base_unified_numerator = numerator.replace(log(Wild('v'), Wild('b')), log(Wild('v'))/log(Wild('b')))
transformed_numerator = base_unified_numerator
if x:
    candidate_logs = [term for term in base_unified_numerator.atoms(log) if term.has(x)]
    if len(candidate_logs) == 1:
        try:
            log_target = candidate_logs[0]
            solutions = solve(transformed_numerator, log_target)
            if solutions:
                rhs = solutions[0]
                transformed_numerator = exp(rhs) - log_target.args[0]
        except Exception:
            pass
# 5. 对最终的分子进行 horner 结构化
final_expr = transformed_numerator
if symbols:
    problem_atoms = {term for term in transformed_numerator.atoms(Function) if any(s in term.free_symbols for s in symbols)}
    dummies = [Dummy() for _ in problem_atoms]
    subs_to_dummy = dict(zip(problem_atoms, dummies))
    poly_expr = transformed_numerator.subs(subs_to_dummy)
    horner_expr = poly_expr
    for s in symbols:
        try:
            horner_expr = horner(horner_expr, wrt=s)
        except Exception:
            pass
    subs_to_original = {v: k for k, v in subs_to_dummy.items()}
    final_expr = horner_expr.subs(subs_to_original)
# 最终结果
str(final_expr)
    `;
        return await pyodide.runPythonAsync(pythonScript);
    }

    // --- 6. SymPy -> WGSL 转换逻辑 (完全保留) ---
    function sympyToWgsl(sympyStr: string): string {
        let wgslStr = sympyStr;
        const powerRegex = /(\([^)]+\)|[\w.-]+)\*\*(\([^)]+\)|[\w.-]+)/;
        while (powerRegex.test(wgslStr)) {
            wgslStr = wgslStr.replace(powerRegex, "pow($1, $2)");
        }
        wgslStr = wgslStr.replace(/\bx\b/g, 'p.x');
        wgslStr = wgslStr.replace(/\by\b/g, 'p.y');
        wgslStr = wgslStr.replace(/\blog\b/g, 'log');
        return wgslStr;
    }

    // --- 7. 主处理流程 (包含验证) ---
    async function processFormula(latex: string, mathJson: any) {
        if (!isEngineReady || !mathJson) return;
        if (!latex.trim()) {
            validationStatus = 'idle';
            return;
        }
        validationStatus = 'pending';

        try {
            const pyodide = $pyodideStore.instance;
            const plainMathJson = JSON.parse(JSON.stringify(mathJson));
            const rawSympyStr = mathJsonToSympyString(plainMathJson);
            const sympyResult = await simplifyWithPyodide(pyodide, rawSympyStr);
            const finalWgsl = sympyToWgsl(sympyResult);

            if (finalWgsl && !finalWgsl.includes('/*') && finalWgsl.trim() !== '') {
                validationStatus = 'valid';
                dispatch('update', { latex: latex, wgsl: finalWgsl });
            } else {
                throw new Error("生成的 WGSL 无效。");
            }
        } catch (err) {
            validationStatus = 'invalid';
            console.error(`公式 "${latex}" 转换失败:`, err);
        }
    }

    // --- 8. 初始化 ---
    onMount(() => {
        MathfieldElement.fontsDirectory = '/fonts';
        const unsubscribe = pyodideStore.subscribe(store => {
            if (store.isReady && mathfieldElement && value) {
                setTimeout(() => {
                    if (mathfieldElement.expression) {
                        processFormula(value, mathfieldElement.expression);
                    }
                }, 1);
                if (unsubscribe) unsubscribe();
            }
        });
    });

    // --- 9. UI 事件处理器 (集成防抖) ---
    function handleMathInputChange(event: Event) {
        const target = event.target as MathfieldElement;
        debounce(() => processFormula(target.value, target.expression), 1);
    }
</script>

<div class="container">
    <math-field
            bind:this={mathfieldElement}
            {value}
            on:input={handleMathInputChange}
            disabled={!isEngineReady}
            title={isLoadingEngine ? '正在加载数学引擎 (全局仅一次)...' : (isEngineReady ? '输入一个隐函数方程' : '数学引擎加载失败')}
    ></math-field>

    <div class="validation-indicator">
        {#if validationStatus === 'pending'}
            <span class="spinner" title="正在验证..."></span>
        {:else if validationStatus === 'valid'}
            <span class="icon valid" title="公式有效">✅</span>
        {:else if validationStatus === 'invalid'}
            <span class="icon invalid" title="公式无效或转换失败">❌</span>
        {/if}
    </div>
</div>

<style>
    .container {
        width: 100%;
        position: relative;
        display: flex;
        align-items: center;
    }
    math-field {
        flex-grow: 1;
        font-size: 1.2rem;
        padding: 0.5rem;
        border-radius: 4px;
        border: 1px solid #ccc;
        box-sizing: border-box;
        --font-family-main: 'STIX Two Text', 'Cambria Math', serif;
        transition: border-color 0.2s, box-shadow 0.2s;
    }
    math-field:focus-within {
        outline: none;
        border-color: #4a90e2;
        box-shadow: 0 0 0 2px rgba(74, 144, 226, 0.2);
    }
    math-field[disabled] {
        background-color: #f0f0f0;
        cursor: wait;
    }
    .validation-indicator {
        width: 24px;
        height: 24px;
        margin-left: 8px;
        display: flex;
        align-items: center;
        justify-content: center;
        flex-shrink: 0;
    }
    .icon {
        font-size: 1.2rem;
    }
    .spinner {
        width: 16px;
        height: 16px;
        border: 2px solid #ccc;
        border-top-color: #4a90e2;
        border-radius: 50%;
        animation: spin 1s linear infinite;
    }
    @keyframes spin {
        to { transform: rotate(360deg); }
    }
</style>