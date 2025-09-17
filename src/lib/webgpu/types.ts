/**
 * 定义核心的 WebGPU 上下文对象，包含设备、画布上下文和纹理格式。
 */
export interface GpuContext {
    device: GPUDevice;
    context: GPUCanvasContext;
    presentationFormat: GPUTextureFormat;
}

/**
 * 定义应用程序中使用的所有 GPU 缓冲区。
 */
export interface GpuBuffers {
    // --- 通用 Uniform 缓冲区 ---
    uniformBuffer: GPUBuffer;

    // --- 点云计算相关缓冲区 ---
    counter_pass1: GPUBuffer;
    point_counter: GPUBuffer;
    point_cloud_buffer: GPUBuffer;
    active_cells_pass1: GPUBuffer;
    indirect_params_staging_buffer: GPUBuffer;
    compute_indirect_buffer: GPUBuffer;
    draw_indirect_buffer: GPUBuffer;

    // --- 网格线顶点缓冲区 ---
    majorGridBuffer: GPUBuffer;
    minorGridBuffer: GPUBuffer;
    axisGridBuffer: GPUBuffer;

    // --- 网格线颜色 Uniform 缓冲区 ---
    minorGridColorBuffer: GPUBuffer;
    majorGridColorBuffer: GPUBuffer;
    axisGridColorBuffer: GPUBuffer;

    // ✅ 新增: 存储函数颜色的调色板缓冲区
    colorPaletteBuffer: GPUBuffer;
}

/**
 * 定义应用程序中使用的所有计算和渲染管线。
 * 注意：这些管线对象是在不同的地方创建的（网格管线在 resources.ts，点云管线在 renderer.ts）。
 */
export interface GpuPipelines {
    // --- 点云计算管线 (动态创建) ---
    compute_pass_1_pipeline: GPUComputePipeline;
    prepare_compute_indirect_pipeline: GPUComputePipeline;
    compute_pass_3_pipeline: GPUComputePipeline;
    prepare_draw_indirect_pipeline: GPUComputePipeline;
    renderPipeline: GPURenderPipeline;

    // --- 网格渲染管线 (静态创建) ---
    gridPipeline: GPURenderPipeline;
}

/**
 * 定义应用程序中使用的所有绑定组。
 */
export interface GpuBindGroups {
    // --- 点云相关绑定组 ---
    computeBindGroup: GPUBindGroup;
    renderBindGroup: GPUBindGroup;

    // --- 网格线相关绑定组 ---
    minorGridBindGroup: GPUBindGroup;
    majorGridBindGroup: GPUBindGroup;
    axisGridBindGroup: GPUBindGroup;
}

/**
 * 定义所有绑定组布局 (BindGroupLayout) 和管线布局 (PipelineLayout)。
 * 布局描述了管线期望的资源接口，在我们的应用中是固定不变的，
 * 因此可以预先创建并复用，即使着色器代码发生变化。
 */
export interface GpuLayouts {
    // --- 绑定组布局 ---
    computeBindGroupLayout: GPUBindGroupLayout;
    pointCloudRenderBindGroupLayout: GPUBindGroupLayout;
    gridBindGroupLayout: GPUBindGroupLayout;

    // --- 管线布局 ---
    computePipelineLayout: GPUPipelineLayout;
    pointCloudRenderPipelineLayout: GPUPipelineLayout;
    gridPipelineLayout: GPUPipelineLayout;
}

/**
 * 将所有 GPU 资源聚合到一个对象中，方便管理和传递。
 * 新增了 `layouts` 属性。
 */
export interface GpuResources {
    buffers: GpuBuffers;
    layouts: GpuLayouts;
    pipelines: GpuPipelines;
    bindGroups: GpuBindGroups;
}