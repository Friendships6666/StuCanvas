<!--src/interaction/menu/AlgebraWindow.svelte-->
<script lang="ts">
    import { formulas, algebraWindowVisible } from '../../stores/ui';
    import type { FormulaEntry, FormulaDomain } from '../../stores/ui';
    import { fly } from 'svelte/transition';
    import { nanoid } from 'nanoid';

    // --- 公式管理 (保持不变) ---
    function addFormula() {
        const newFormula: FormulaEntry = {
            id: nanoid(8),
            expression: 'y = x^2',
            domains: [],
            enabled: true,
        };
        formulas.update(fs => [...fs, newFormula]);
    }

    function deleteFormula(id: string) {
        formulas.update(fs => fs.filter(f => f.id !== id));
    }

    // --- 定义域管理 (保持不变) ---
    function addDomain(formulaId: string) {
        const newDomain: FormulaDomain = {
            id: nanoid(8),
            rawString: 'x > 0',
        };
        formulas.update(fs => fs.map(f =>
            f.id === formulaId ? { ...f, domains: [...f.domains, newDomain] } : f
        ));
    }

    function deleteDomain(formulaId: string, domainId: string) {
        formulas.update(fs => fs.map(f =>
            f.id === formulaId ? { ...f, domains: f.domains.filter(d => d.id !== domainId) } : f
        ));
    }

    // ✅ *** 核心修复：创建专门的、不可变的更新函数 ***
    function updateEnabled(formulaId: string, newEnabled: boolean) {
        formulas.update(currentFormulas =>
            currentFormulas.map(f =>
                f.id === formulaId ? { ...f, enabled: newEnabled } : f
            )
        );
    }

    function updateExpression(formulaId: string, newExpression: string) {
        formulas.update(currentFormulas =>
            currentFormulas.map(f =>
                f.id === formulaId ? { ...f, expression: newExpression } : f
            )
        );
    }

    function updateDomainString(formulaId: string, domainId: string, newRawString: string) {
        formulas.update(currentFormulas =>
            currentFormulas.map(f =>
                f.id === formulaId
                    ? { ...f, domains: f.domains.map(d => d.id === domainId ? { ...d, rawString: newRawString } : d) }
                    : f
            )
        );
    }
</script>

{#if $algebraWindowVisible}
    <div class="window" transition:fly={{ y: -20, duration: 250, opacity: 0 }}>
        <div class="header">
            <span class="title">代数区</span>
            <button class="close-btn" on:click={() => algebraWindowVisible.set(false)} title="关闭">
                <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12"/></svg>
            </button>
        </div>

        <div class="content">
            {#each $formulas as formula (formula.id)}
                <div class="formula-entry" class:disabled={!formula.enabled}>
                    <div class="main-row">
                        <!-- ✅ 核心修复：用 on:change 替换 bind:checked -->
                        <input type="checkbox" checked={formula.enabled} on:change={(e) => updateEnabled(formula.id, e.currentTarget.checked)} class="toggle" title="启用/禁用此公式"/>

                        <!-- ✅ 核心修复：用 on:input 替换 bind:value -->
                        <input type="text" value={formula.expression} on:input={(e) => updateExpression(formula.id, e.currentTarget.value)} class="expression-input" placeholder="例如 y = sin(x)"/>

                        <button on:click={() => deleteFormula(formula.id)} class="action-btn delete" title="删除公式">
                            <svg viewBox="0 0 24 24"><path d="M18 12H6"/></svg>
                        </button>
                    </div>

                    <div class="domains-section">
                        {#each formula.domains as domain (domain.id)}
                            <div class="domain-row">
                                <span class="domain-label">x ∈</span>
                                <!-- ✅ 核心修复：用 on:input 替换 bind:value -->
                                <input type="text" value={domain.rawString} on:input={(e) => updateDomainString(formula.id, domain.id, e.currentTarget.value)} class="domain-input" placeholder="例如 0 < x < 2"/>
                                <button on:click={() => deleteDomain(formula.id, domain.id)} class="action-btn delete-small" title="删除此区间">
                                    <svg viewBox="0 0 24 24"><path d="M18 12H6"/></svg>
                                </button>
                            </div>
                        {/each}
                        <button on:click={() => addDomain(formula.id)} class="add-domain-btn">
                            + 添加取值区间
                        </button>
                    </div>
                </div>
            {/each}
        </div>

        <div class="footer">
            <button on:click={addFormula} class="add-formula-btn">
                <svg viewBox="0 0 24 24"><path d="M12 6v12M6 12h12"/></svg>
                <span>添加公式</span>
            </button>
        </div>
    </div>
{/if}


<style>
    .window {
        position: fixed;
        top: 20px;
        left: 20px;
        width: 380px;
        max-height: calc(100vh - 40px);
        display: flex;
        flex-direction: column;
        border-radius: 12px;
        border: 1px solid rgba(255, 255, 255, 0.5);
        background: radial-gradient(circle at 50% 0%, hsla(200, 80%, 88%, 0.7), hsla(215, 90%, 80%, 0.8));
        backdrop-filter: blur(16px);
        -webkit-backdrop-filter: blur(16px);
        box-shadow: 0 8px 32px 0 rgba(100, 130, 200, 0.3);
        color: #1a3b5a;
        font-family: 'Segoe UI', 'Frutiger', 'Frutiger Linotype', 'Dejavu Sans', 'Helvetica Neue', Arial, sans-serif;
    }

    .header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 8px 12px;
        border-bottom: 1px solid rgba(255, 255, 255, 0.3);
        cursor: grab;
    }
    .header:active { cursor: grabbing; }

    .title {
        font-weight: 600;
        font-size: 16px;
        text-shadow: 0 1px 1px rgba(255, 255, 255, 0.7);
    }

    .content {
        padding: 8px;
        overflow-y: auto;
        display: flex;
        flex-direction: column;
        gap: 10px;
    }

    .formula-entry {
        background: rgba(255, 255, 255, 0.3);
        border-radius: 8px;
        padding: 10px;
        transition: all 0.2s ease-in-out;
    }
    .formula-entry.disabled {
        opacity: 0.5;
    }

    .main-row, .domain-row {
        display: flex;
        align-items: center;
        gap: 8px;
    }

    input[type="text"] {
        flex-grow: 1;
        background: rgba(255, 255, 255, 0.5);
        border: 1px solid rgba(255, 255, 255, 0.4);
        border-radius: 6px;
        padding: 6px 10px;
        font-size: 15px;
        color: #0b2a4a;
        box-shadow: inset 0 1px 3px rgba(0,0,0,0.1);
        outline: none;
        transition: all 0.2s;
    }
    input[type="text"]:focus {
        border-color: rgba(59, 130, 246, 0.7);
        box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.3);
    }

    .color-picker-wrapper {
        width: 28px;
        height: 28px;
        border-radius: 6px;
        padding: 2px;
        background: linear-gradient(145deg, rgba(255,255,255,0.6), rgba(255,255,255,0.1));
        box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }
    .color-picker {
        width: 100%;
        height: 100%;
        border: none;
        border-radius: 4px;
        cursor: pointer;
    }

    .toggle {
        width: 20px;
        height: 20px;
        accent-color: #2563eb;
    }

    .domains-section {
        padding-left: 30px;
        margin-top: 8px;
        display: flex;
        flex-direction: column;
        gap: 6px;
    }
    .domain-label {
        font-style: italic;
        color: #3a5b7a;
    }

    button {
        border: 1px solid rgba(0,0,0,0.1);
        border-radius: 6px;
        cursor: pointer;
        transition: all 0.15s ease;
        display: flex;
        align-items: center;
        justify-content: center;
        font-weight: 600;
    }
    button:hover {
        transform: translateY(-1px);
        box-shadow: 0 2px 6px rgba(0,0,0,0.1);
    }
    button:active {
        transform: translateY(0);
        box-shadow: inset 0 1px 2px rgba(0,0,0,0.15);
    }

    .action-btn {
        flex-shrink: 0;
        width: 28px;
        height: 28px;
        padding: 0;
    }
    .action-btn.delete, .action-btn.delete-small {
        background: linear-gradient(180deg, #ff8a8a, #e53e3e);
        color: white;
    }
    .action-btn.delete-small {
        width: 24px;
        height: 24px;
    }
    .action-btn svg {
        width: 16px;
        height: 16px;
        stroke: currentColor;
        stroke-width: 2.5;
        stroke-linecap: round;
    }

    .add-domain-btn {
        margin-top: 4px;
        align-self: flex-start;
        padding: 4px 10px;
        font-size: 12px;
        background: rgba(255, 255, 255, 0.4);
        color: #1a3b5a;
    }

    .footer {
        padding: 8px;
        border-top: 1px solid rgba(255, 255, 255, 0.3);
    }
    .add-formula-btn {
        width: 100%;
        padding: 8px;
        font-size: 15px;
        gap: 8px;
        background: linear-gradient(180deg, #86efac, #22c55e);
        color: white;
        text-shadow: 0 1px 1px rgba(0,0,0,0.2);
    }
    .add-formula-btn svg {
        width: 18px;
        height: 18px;
        stroke-width: 2;
    }

    .close-btn {
        width: 24px;
        height: 24px;
        background: none;
        border: none;
        color: #1a3b5a;
        opacity: 0.7;
    }
    .close-btn:hover {
        opacity: 1;
        background: rgba(255,255,255,0.3);
        transform: none;
        box-shadow: none;
    }
    .close-btn svg {
        width: 100%;
        height: 100%;
        stroke-width: 2.5;
    }

    /* Custom Scrollbar for Frutiger Aero */
    .content::-webkit-scrollbar {
        width: 12px;
    }
    .content::-webkit-scrollbar-track {
        background: rgba(255, 255, 255, 0.1);
        border-radius: 10px;
    }
    .content::-webkit-scrollbar-thumb {
        background: linear-gradient(180deg, hsla(210, 80%, 85%, 0.9), hsla(210, 80%, 70%, 0.9));
        border-radius: 10px;
        border: 2px solid rgba(255, 255, 255, 0.2);
    }
</style>