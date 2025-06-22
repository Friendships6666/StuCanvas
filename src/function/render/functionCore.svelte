<script lang="ts">
    import { onDestroy } from 'svelte';
    import type { Writable } from 'svelte/store';
    import type { View } from '../../stores/camera';
    import type { Renderable } from '../../renderCore/renderer';
    import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';
    import { initializeUnifiedResources, type UnifiedFunctionResources } from '../unifiedRenderer';

    export let register: Set<Renderable>;
    export let device: GPUDevice;
    export let canvasFormat: GPUTextureFormat;
    export let sampleCount: number;
    export let canvasElement: HTMLCanvasElement;
    export let view: Writable<View>;
    export let aspect: number;
    export let formulas: DrawableFormula[];
    export let requestRender: () => void;

    let unifiedResources: UnifiedFunctionResources | null = null;
    let isInitializing = false;

    $: if (device && formulas) {
        initialize();
    }

    async function initialize() {
        if (isInitializing) return;
        isInitializing = true;

        const oldResources = unifiedResources;
        unifiedResources = null;

        if (oldResources) {
            register.delete(oldResources.renderable);
            oldResources.uniformBuffer.destroy();
            oldResources.maskResources.texture.destroy();
            oldResources.sdfResources.functionDataBuffer.destroy();
        }

        if (formulas.length === 0) {
            requestRender();
            isInitializing = false;
            return;
        }

        try {
            const newResources = await initializeUnifiedResources(
                device, canvasFormat, sampleCount, formulas, canvasElement
            );
            if (newResources) {
                unifiedResources = newResources;
                register.add(newResources.renderable);
            }
        } catch (e) {
            console.error("Failed to initialize unified function resources:", e);
        } finally {
            isInitializing = false;
        }
    }

    $: if (unifiedResources && $view) {
        const uniformBuffer = unifiedResources.uniformBuffer;

        const x_range_half = aspect / $view.zoom;
        const x_min = $view.x - x_range_half;
        const x_max = $view.x + x_range_half;

        const formulaParams = { x_min: x_min, x_max: x_max, num_points: 200000 };

        const uniformBufferSize = 256;
        const allUniformData = new Float32Array(uniformBufferSize / 4 * (1 + formulas.length));

        const sdfData = new Float32Array([
            $view.x, $view.y, $view.zoom, aspect,
            canvasElement.width, canvasElement.height, 0, 0,
            formulaParams.x_min, formulaParams.x_max, formulaParams.num_points, 0
        ]);
        allUniformData.set(sdfData, 0);

        for (let i = 0; i < formulas.length; i++) {
            const formulaData = new Float32Array([
                $view.x, $view.y, $view.zoom, aspect,
                canvasElement.width, canvasElement.height, 0, 0,
                formulaParams.x_min, formulaParams.x_max, formulaParams.num_points, i
            ]);
            allUniformData.set(formulaData, (i + 1) * (uniformBufferSize / 4));
        }

        device.queue.writeBuffer(uniformBuffer, 0, allUniformData);
        requestRender();
    }

    onDestroy(() => {
        if (unifiedResources) {
            register.delete(unifiedResources.renderable);
            unifiedResources.uniformBuffer.destroy();
            unifiedResources.maskResources.texture.destroy();
            unifiedResources.sdfResources.functionDataBuffer.destroy();
            unifiedResources = null;
        }
    });
</script>