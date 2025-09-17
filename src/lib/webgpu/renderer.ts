import { CLEAR_COLOR, COMPUTE_DISPATCH_WIDTH, MAJOR_GRID_COLOR, MINOR_GRID_COLOR, AXIS_COLOR } from './config';
import { initializeWebGpuContext } from './device';
import { createGpuResources } from './resources';
import { downloadPointCloudData } from './debug';
import type { GpuContext, GpuResources } from './types';
import type { GridLineData } from '../grid';

export class Renderer {
    private context!: GpuContext;
    private resources!: GpuResources;

    public isInitialized = false;

    /**
     * 初始化 WebGPU 上下文、资源和初始管线。
     */
    public async initialize(canvas: HTMLCanvasElement, pointCloudShaderCode: string, gridShaderCode: string): Promise<boolean> {
        const gpuContext = await initializeWebGpuContext(canvas);
        if (!gpuContext) return false;
        this.context = gpuContext;

        const gridShaderModule = gridShaderCode ? this.context.device.createShaderModule({ code: gridShaderCode }) : null;

        this.resources = createGpuResources(this.context, canvas, gridShaderModule);

        const pointCloudShaderModule = this.context.device.createShaderModule({ code: pointCloudShaderCode });
        this.recreatePointCloudPipelines(pointCloudShaderModule);

        if (this.resources.pipelines.gridPipeline) {
            this.context.device.queue.writeBuffer(this.resources.buffers.minorGridColorBuffer, 0, new Float32Array([MINOR_GRID_COLOR.r, MINOR_GRID_COLOR.g, MINOR_GRID_COLOR.b, MINOR_GRID_COLOR.a]));
            this.context.device.queue.writeBuffer(this.resources.buffers.majorGridColorBuffer, 0, new Float32Array([MAJOR_GRID_COLOR.r, MAJOR_GRID_COLOR.g, MAJOR_GRID_COLOR.b, MAJOR_GRID_COLOR.a]));
            this.context.device.queue.writeBuffer(this.resources.buffers.axisGridColorBuffer, 0, new Float32Array([AXIS_COLOR.r, AXIS_COLOR.g, AXIS_COLOR.b, AXIS_COLOR.a]));
        }

        this.isInitialized = true;
        return true;
    }

    /**
     * 重新编译点云着色器并重新创建必要的管线。
     */
    public async updatePointCloudShader(pointCloudShaderCode: string) {
        if (!this.isInitialized) {
            console.error("渲染器未初始化，无法更新着色器。");
            return;
        }

        try {
            const pointCloudShaderModule = this.context.device.createShaderModule({ code: pointCloudShaderCode });
            this.recreatePointCloudPipelines(pointCloudShaderModule);
            console.log("已成功使用新着色器更新 GPU 管线。");
        } catch (error) {
            console.error("创建新着色器模块或管线失败：", error);
            throw error;
        }
    }

    /**
     * 一个私有辅助方法，用于创建/重新创建所有依赖于点云着色器的管线。
     */
    private recreatePointCloudPipelines(pointCloudModule: GPUShaderModule) {
        const { device, presentationFormat } = this.context;
        const { computePipelineLayout, pointCloudRenderPipelineLayout } = this.resources.layouts;

        this.resources.pipelines.compute_pass_1_pipeline = device.createComputePipeline({
            layout: computePipelineLayout,
            compute: { module: pointCloudModule, entryPoint: 'compute_pass_1' },
        });
        this.resources.pipelines.prepare_compute_indirect_pipeline = device.createComputePipeline({
            layout: computePipelineLayout,
            compute: { module: pointCloudModule, entryPoint: 'prepare_compute_indirect' },
        });
        this.resources.pipelines.compute_pass_3_pipeline = device.createComputePipeline({
            layout: computePipelineLayout,
            compute: { module: pointCloudModule, entryPoint: 'compute_pass_3' },
        });
        this.resources.pipelines.prepare_draw_indirect_pipeline = device.createComputePipeline({
            layout: computePipelineLayout,
            compute: { module: pointCloudModule, entryPoint: 'prepare_draw_indirect' },
        });

        this.resources.pipelines.renderPipeline = device.createRenderPipeline({
            layout: pointCloudRenderPipelineLayout,
            vertex: {
                module: pointCloudModule,
                entryPoint: 'vs_main',
            },
            fragment: {
                module: pointCloudModule,
                entryPoint: 'fs_main',
                targets: [{
                    format: presentationFormat,
                    blend: {
                        color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                        alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                    },
                }],
            },
            primitive: {
                topology: 'triangle-list',
            },
        });
    }


    /**
     * 主渲染循环函数。
     * 签名已更新，以接受第6个参数 gridData。
     */
    public render(
        canvas: HTMLCanvasElement,
        zoom: number,
        offset: { x: number, y: number },
        numFunctions: number,
        colors: number[][],
        gridData: GridLineData
    ) {
        if (!this.isInitialized) {
            return;
        }

        const { device } = this.context;
        const { buffers, pipelines, bindGroups } = this.resources;

        // 使用从 App.svelte 传入的 gridData
        const { majorVertices, minorVertices, axisVertices } = gridData;
        const majorGridVertexCount = majorVertices.length / 2;
        const minorGridVertexCount = minorVertices.length / 2;
        const axisGridVertexCount = axisVertices.length / 2;

        if (pipelines.gridPipeline) {
            device.queue.writeBuffer(buffers.majorGridBuffer, 0, majorVertices);
            device.queue.writeBuffer(buffers.minorGridBuffer, 0, minorVertices);
            device.queue.writeBuffer(buffers.axisGridBuffer, 0, axisVertices);
        }

        // Uniform buffer 写入
        const UniformsValues = new ArrayBuffer(32);
        const UniformsViews = {
            screen_dimensions: new Float32Array(UniformsValues, 0, 2),
            zoom: new Float32Array(UniformsValues, 8, 1),
            offset: new Float32Array(UniformsValues, 16, 2),
            num_functions: new Uint32Array(UniformsValues, 24, 1),
            dispatch_width: new Uint32Array(UniformsValues, 28, 1),
        };
        UniformsViews.screen_dimensions.set([canvas.width, canvas.height]);
        UniformsViews.zoom.set([zoom]);
        UniformsViews.offset.set([offset.x, offset.y]);
        UniformsViews.num_functions.set([numFunctions]);
        UniformsViews.dispatch_width.set([COMPUTE_DISPATCH_WIDTH]);
        device.queue.writeBuffer(buffers.uniformBuffer, 0, UniformsValues);

        if (colors.length > 0) {
            const colorData = new Float32Array(colors.flat());
            device.queue.writeBuffer(buffers.colorPaletteBuffer, 0, colorData);
        }

        // 重置原子计数器
        device.queue.writeBuffer(buffers.counter_pass1, 0, new Uint32Array([0]));
        device.queue.writeBuffer(buffers.point_counter, 0, new Uint32Array([0]));

        const commandEncoder = device.createCommandEncoder();

        // --- 计算通道 ---
        const pass1 = commandEncoder.beginComputePass();
        pass1.setPipeline(pipelines.compute_pass_1_pipeline);
        pass1.setBindGroup(0, bindGroups.computeBindGroup);
        pass1.dispatchWorkgroups(Math.ceil(canvas.width / 16), Math.ceil(canvas.height / 16));
        pass1.end();

        const pass2 = commandEncoder.beginComputePass();
        pass2.setPipeline(pipelines.prepare_compute_indirect_pipeline);
        pass2.setBindGroup(0, bindGroups.computeBindGroup);
        pass2.dispatchWorkgroups(1);
        pass2.end();

        commandEncoder.copyBufferToBuffer(buffers.indirect_params_staging_buffer, 0, buffers.compute_indirect_buffer, 0, 12);

        const pass3 = commandEncoder.beginComputePass();
        pass3.setPipeline(pipelines.compute_pass_3_pipeline);
        pass3.setBindGroup(0, bindGroups.computeBindGroup);
        pass3.dispatchWorkgroupsIndirect(buffers.compute_indirect_buffer, 0);
        pass3.end();

        const pass4 = commandEncoder.beginComputePass();
        pass4.setPipeline(pipelines.prepare_draw_indirect_pipeline);
        pass4.setBindGroup(0, bindGroups.computeBindGroup);
        pass4.dispatchWorkgroups(1);
        pass4.end();

        commandEncoder.copyBufferToBuffer(buffers.indirect_params_staging_buffer, 12, buffers.draw_indirect_buffer, 0, 16);

        // --- 渲染通道 ---
        const renderPass = commandEncoder.beginRenderPass({
            colorAttachments: [{
                view: this.context.context.getCurrentTexture().createView(),
                loadOp: 'clear',
                storeOp: 'store',
                clearValue: CLEAR_COLOR
            }]
        });

        // 绘制网格线
        if (pipelines.gridPipeline) {
            renderPass.setPipeline(pipelines.gridPipeline);
            if (minorGridVertexCount > 0) { renderPass.setBindGroup(0, bindGroups.minorGridBindGroup); renderPass.setVertexBuffer(0, buffers.minorGridBuffer); renderPass.draw(minorGridVertexCount); }
            if (majorGridVertexCount > 0) { renderPass.setBindGroup(0, bindGroups.majorGridBindGroup); renderPass.setVertexBuffer(0, buffers.majorGridBuffer); renderPass.draw(majorGridVertexCount); }
            if (axisGridVertexCount > 0) { renderPass.setBindGroup(0, bindGroups.axisGridBindGroup); renderPass.setVertexBuffer(0, buffers.axisGridBuffer); renderPass.draw(axisGridVertexCount); }
        }

        // 绘制点云
        renderPass.setPipeline(pipelines.renderPipeline);
        renderPass.setBindGroup(1, bindGroups.renderBindGroup);
        renderPass.drawIndirect(buffers.draw_indirect_buffer, 0);

        renderPass.end();
        device.queue.submit([commandEncoder.finish()]);
    }

    /**
     * 用于调试的辅助函数，以下载点云数据。
     */
    public downloadData() {
        if (!this.isInitialized) { console.error("渲染器未初始化。"); return; }
        downloadPointCloudData(this.context.device, this.resources.buffers);
    }
}