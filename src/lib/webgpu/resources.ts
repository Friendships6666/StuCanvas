import { POINT_MULTIPLIER, MAX_GRID_VERTICES, MAX_FUNCTIONS } from './config'; // 假设 MAX_FUNCTIONS 在 config 中定义
import type { GpuBuffers, GpuContext, GpuPipelines, GpuBindGroups, GpuResources, GpuLayouts } from './types';

/**
 * 创建所有核心 GPU 资源，包括缓冲区、布局、静态管线和绑定组。
 * @param {GpuContext} context - WebGPU 上下文对象。
 * @param {HTMLCanvasElement} canvas - HTML 画布元素。
 * @param {GPUShaderModule} gridShaderModule - 预编译的网格着色器模块。
 * @returns {GpuResources} - 包含所有已创建资源的聚合对象。
 */
export function createGpuResources(
    { device, presentationFormat }: GpuContext,
    canvas: HTMLCanvasElement,
    gridShaderModule: GPUShaderModule | null
): GpuResources {
    const buffers = createBuffers(device, canvas);
    const layouts = createLayouts(device);
    const pipelines = createPipelines(device, presentationFormat, gridShaderModule, layouts);
    const bindGroups = createBindGroups(device, layouts, buffers);

    // 我们使用类型断言 `as GpuPipelines` 来满足 GpuResources 接口。
    // 这是安全的，因为我们知道 Renderer 会在此函数调用后立即填充
    // 剩余的点云管线。
    return { buffers, layouts, pipelines: pipelines as GpuPipelines, bindGroups };
}

/**
 * 创建应用程序所需的所有 GPU 缓冲区。
 * @param {GPUDevice} device - WebGPU 设备。
 * @param {HTMLCanvasElement} canvas - HTML 画布元素。
 * @returns {GpuBuffers} - 包含所有缓冲区的对象。
 */
function createBuffers(device: GPUDevice, canvas: HTMLCanvasElement): GpuBuffers {
    const maxCells = canvas.width * canvas.height;
    const maxPoints = maxCells * POINT_MULTIPLIER;

    // ✅ 核心变化: 更新点云缓冲区大小计算。
    // PointData 结构体 { pos: vec2<f32>, index: u32 } 在 WGSL 存储缓冲区中
    // 需要16字节对齐（vec2<f32>对齐为8，但整个结构体大小12会向上取整到16）。
    const pointCloudBufferSize = maxPoints * 16; // 每个点16字节

    if (pointCloudBufferSize > device.limits.maxStorageBufferBindingSize) {
        throw new Error(`请求的点云缓冲区大小 (${pointCloudBufferSize} 字节) 超出设备限制 (${device.limits.maxStorageBufferBindingSize} 字节)。`);
    }

    return {
        uniformBuffer: device.createBuffer({ size: 32, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        counter_pass1: device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC }),
        active_cells_pass1: device.createBuffer({ size: maxCells * 8, usage: GPUBufferUsage.STORAGE }),
        indirect_params_staging_buffer: device.createBuffer({ size: 32, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC }),
        compute_indirect_buffer: device.createBuffer({ size: 12, usage: GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST }),
        draw_indirect_buffer: device.createBuffer({ size: 16, usage: GPUBufferUsage.INDIRECT | GPUBufferUsage.COPY_DST }),
        point_counter: device.createBuffer({ size: 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC }),
        point_cloud_buffer: device.createBuffer({ size: pointCloudBufferSize, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_SRC }),

        majorGridBuffer: device.createBuffer({ size: MAX_GRID_VERTICES * Float32Array.BYTES_PER_ELEMENT, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }),
        minorGridBuffer: device.createBuffer({ size: MAX_GRID_VERTICES * Float32Array.BYTES_PER_ELEMENT, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }),
        axisGridBuffer: device.createBuffer({ size: 4 * 2 * Float32Array.BYTES_PER_ELEMENT, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST }),

        minorGridColorBuffer: device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        majorGridColorBuffer: device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),
        axisGridColorBuffer: device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }),

        // ✅ 新增: 存储函数颜色的调色板缓冲区
        colorPaletteBuffer: device.createBuffer({
            size: MAX_FUNCTIONS * 16, // MAX_FUNCTIONS 个 vec4<f32> 颜色
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        }),
    };
}

/**
 * 创建所有管线和绑定组布局。这些是静态且可复用的。
 * @param {GPUDevice} device - WebGPU 设备。
 * @returns {GpuLayouts} - 包含所有布局的对象。
 */
function createLayouts(device: GPUDevice): GpuLayouts {
    const computeBindGroupLayout = device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 5, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
        ]
    });

    const pointCloudRenderBindGroupLayout = device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.VERTEX, buffer: { type: 'read-only-storage' } },
            // ✅ 新增: 颜色调色板的布局，仅在片元着色器中需要
            { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        ]
    });

    const gridBindGroupLayout = device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
        ]
    });

    const computePipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [computeBindGroupLayout] });
    const pointCloudRenderPipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [/* @group(0) is unused */, pointCloudRenderBindGroupLayout] });
    const gridPipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [gridBindGroupLayout] });

    return {
        computeBindGroupLayout,
        pointCloudRenderBindGroupLayout,
        gridBindGroupLayout,
        computePipelineLayout,
        pointCloudRenderPipelineLayout,
        gridPipelineLayout
    };
}


/**
 * 仅创建静态的网格管线。
 * @param {GPUDevice} device - WebGPU 设备。
 * @param {GPUTextureFormat} format - 画布的呈现格式。
 * @param {GPUShaderModule} gridModule - 预编译的网格着色器模块。
 * @param {GpuLayouts} layouts - 包含预创建布局的对象。
 * @returns {Partial<GpuPipelines>} - 一个部分填充的管线对象。
 */
function createPipelines(device: GPUDevice, format: GPUTextureFormat, gridModule: GPUShaderModule | null, layouts: GpuLayouts): Partial<GpuPipelines> {
    const partialPipelines: Partial<GpuPipelines> = {};

    if (gridModule) {
        partialPipelines.gridPipeline = device.createRenderPipeline({
            layout: layouts.gridPipelineLayout,
            vertex: {
                module: gridModule,
                entryPoint: 'vs_main',
                buffers: [{
                    arrayStride: 8,
                    attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x2' }],
                }],
            },
            fragment: {
                module: gridModule,
                entryPoint: 'fs_main',
                targets: [{ format }],
            },
            primitive: {
                topology: 'line-list',
            },
        });
    }

    return partialPipelines;
}


/**
 * 创建应用程序所需的所有绑定组。
 * @param {GPUDevice} device - WebGPU 设备。
 * @param {GpuLayouts} layouts - 包含预创建布局的对象。
 * @param {GpuBuffers} buffers - 包含预创建缓冲区的对象。
 * @returns {GpuBindGroups} - 包含所有绑定组的对象。
 */
function createBindGroups(device: GPUDevice, layouts: GpuLayouts, buffers: GpuBuffers): GpuBindGroups {
    return {
        computeBindGroup: device.createBindGroup({
            layout: layouts.computeBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: buffers.uniformBuffer } },
                { binding: 1, resource: { buffer: buffers.counter_pass1 } },
                { binding: 2, resource: { buffer: buffers.active_cells_pass1 } },
                { binding: 3, resource: { buffer: buffers.indirect_params_staging_buffer } },
                { binding: 4, resource: { buffer: buffers.point_counter } },
                { binding: 5, resource: { buffer: buffers.point_cloud_buffer } },
            ],
        }),
        renderBindGroup: device.createBindGroup({
            layout: layouts.pointCloudRenderBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: buffers.uniformBuffer } },
                { binding: 1, resource: { buffer: buffers.point_cloud_buffer } },
                // ✅ 新增: 绑定颜色调色板缓冲区
                { binding: 2, resource: { buffer: buffers.colorPaletteBuffer } },
            ],
        }),
        minorGridBindGroup: device.createBindGroup({
            layout: layouts.gridBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: buffers.uniformBuffer } },
                { binding: 1, resource: { buffer: buffers.minorGridColorBuffer } },
            ],
        }),
        majorGridBindGroup: device.createBindGroup({
            layout: layouts.gridBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: buffers.uniformBuffer } },
                { binding: 1, resource: { buffer: buffers.majorGridColorBuffer } },
            ],
        }),
        axisGridBindGroup: device.createBindGroup({
            layout: layouts.gridBindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: buffers.uniformBuffer } },
                { binding: 1, resource: { buffer: buffers.axisGridColorBuffer } },
            ],
        }),
    };
}