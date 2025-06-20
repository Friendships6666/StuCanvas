<!--src/coordinate/rectangular/grid/GridLineRenderer.svelte-->
<script lang="ts">
    /// <reference types="@webgpu/types" />
    import { onMount, onDestroy } from 'svelte';
    import { hexToVec4 } from '../../../utils/color-utils';
    import type { Renderable } from '../../../renderCore/renderer';

    // --- Props (组件的输入属性) ---

    /**
     * 一个 Set，用于将此组件的渲染能力“注册”到主渲染器。
     */
    export let register: Set<Renderable>;

    /**
     * 从主渲染器传入的 GPUDevice 实例。
     */
    export let device: GPUDevice;

    /**
     * 从主渲染器传入的画布颜色格式。
     */
    export let canvasFormat: GPUTextureFormat;

    /**
     * 从主渲染器传入的抗锯齿采样数。
     */
    export let sampleCount: number;

    /**
     * 该网格线图层的渲染顺序，数值越小越先渲染。
     */
    export let layer: number;

    /**
     * 网格线的颜色 (十六进制字符串，如 #cccccc)。
     */
    export let color: string;

    /**
     * 一个包含所有线段端点坐标的 Float32Array。
     * 格式为 [x1, y1, x2, y2, x3, y3, ...]
     * 这个 prop 是响应式的，当它变化时会触发资源更新。
     */
    export let vertices: Float32Array;

    // --- 内部 GPU 资源 (组件的私有状态) ---
    let pipeline: GPURenderPipeline;
    let vertexBuffer: GPUBuffer;
    let uniformBuffer: GPUBuffer;
    let bindGroup: GPUBindGroup;

    /**
     * 初始化函数，只在组件第一次接收到有效 device 时运行一次。
     * 它负责创建所有可复用的 GPU 资源。
     */
    function initialize() {
        // 创建用于存储颜色的 uniform buffer
        uniformBuffer = device.createBuffer({
            label: `GridLine Uniform Buffer (Layer ${layer})`,
            size: 16, // 一个 vec4f 需要 16 字节
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });

        // WGSL 着色器代码
        const shaderModule = device.createShaderModule({
            label: `GridLine Shader (Layer ${layer})`,
            code: `
                // 定义 uniform buffer 的结构体
                struct Uniforms {
                    color: vec4f // f32 的别名
                };
                @group(0) @binding(0) var<uniform> u: Uniforms;

                // 顶点着色器：接收 NDC 坐标并直接透传
                @vertex
                fn vs_main(@location(0) clip_pos: vec2f) -> @builtin(position) vec4f {
                    return vec4f(clip_pos, 0.0, 1.0);
                }

                // 片元着色器：为线上的每个像素返回统一的颜色
                @fragment
                fn fs_main() -> @location(0) vec4f {
                  return u.color;
                }
            `,
        });

        // 创建绑定组布局，描述着色器需要哪些资源
        const bindGroupLayout = device.createBindGroupLayout({
            entries: [{
                binding: 0,
                visibility: GPUShaderStage.FRAGMENT, // 颜色只在片元着色器中使用
                buffer: { type: 'uniform' }
            }]
        });

        // 创建渲染管线，这是最核心的配置步骤
        pipeline = device.createRenderPipeline({
            label: `GridLine Pipeline (Layer ${layer})`,
            layout: device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }),
            vertex: {
                module: shaderModule,
                entryPoint: 'vs_main',
                buffers: [{ // 描述顶点数据的内存布局
                    arrayStride: 8, // 每个顶点(vec2f)占 2 * 4 = 8 字节
                    attributes: [{
                        shaderLocation: 0, // 对应 WGSL 中 @location(0)
                        offset: 0,
                        format: 'float32x2' // 顶点格式为 vec2<f32>
                    }]
                }]
            },
            fragment: {
                module: shaderModule,
                entryPoint: 'fs_main',
                targets: [{ format: canvasFormat }] // 输出颜色格式与画布匹配
            },
            primitive: {
                // ✅ 核心改变：告诉 GPU 如何解释顶点数据
                topology: 'line-list' // 将每两个顶点绘制成一条独立的线段
            },
            multisample: { count: sampleCount },
        });

        // 创建绑定组，将实际的 uniform buffer 绑定到布局
        bindGroup = device.createBindGroup({
            label: `GridLine BindGroup (Layer ${layer})`,
            layout: pipeline.getBindGroupLayout(0),
            entries: [{
                binding: 0,
                resource: { buffer: uniformBuffer }
            }],
        });
    }

    // --- 响应式更新逻辑 ($: 语句) ---
    // 这个 $: 代码块会“监听” device 和 vertices 的变化。
    // 当它们任何一个有效时，就会执行此逻辑。
    $: if (device && vertices) {
        // 1. 如果 pipeline 不存在，说明是首次运行，立即进行初始化。
        if (!pipeline) {
            initialize();
        }

        // 2. 动态管理顶点缓冲区 (Vertex Buffer) 的大小
        // 如果没有缓冲区，或者新的顶点数据装不下了，就创建一个更大的新缓冲区
        if (!vertexBuffer || vertices.byteLength > vertexBuffer.size) {
            // 如果旧缓冲区存在，先销毁它释放 GPU 内存
            vertexBuffer?.destroy();

            // 为了避免频繁地重新创建，我们给新缓冲区 1.5 倍的容量
            const newCapacity = Math.max(64, vertices.byteLength * 1.5);
            vertexBuffer = device.createBuffer({
                label: `GridLine Vertex Buffer (Layer ${layer})`,
                size: newCapacity,
                usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
            });
        }

        // 3. 将最新的数据写入 GPU 的 Buffer 中
        // 这是每一帧或每次数据更新时都会发生的操作
        device.queue.writeBuffer(vertexBuffer, 0, vertices);
        device.queue.writeBuffer(uniformBuffer, 0, hexToVec4(color)); // 颜色也可能动态改变
    }

    // --- Renderable 接口的实现 ---
    /**
     * 定义一个 draw 函数，它会被主渲染循环调用。
     * 它的职责纯粹而高效：下达绘制命令。
     */
    function draw(pass: GPURenderPassEncoder) {
        // 如果资源未就绪或没有顶点可画，则直接返回
        if (!pipeline || !vertexBuffer || vertices.length === 0) return;

        pass.setPipeline(pipeline);
        pass.setVertexBuffer(0, vertexBuffer);
        pass.setBindGroup(0, bindGroup);
        // 这里的 draw 调用现在绘制的是线段
        pass.draw(vertices.length / 2); // 顶点总数 (每个顶点有x, y两个分量)
    }

    // --- Svelte 生命周期钩子 ---
    onMount(() => {
        // 创建一个符合 Renderable 接口的对象
        const self: Renderable = {
            draw,
            layer // 把 layer 属性也放进去
        };

        // 将自己“注册”到父组件的渲染列表中
        register.add(self);

        // onMount 可以返回一个函数，这个函数会在组件销毁时被调用
        // 这是一种非常优雅的清理模式
        return () => {
            register.delete(self);
        };
    });

    onDestroy(() => {
        // 当组件被销毁时，确保异步地释放所有 GPU 资源，防止内存泄漏。
        // onSubmittedWorkDone() 确保在销毁前，GPU 已经完成了对这些资源的最后使用。
        device?.queue.onSubmittedWorkDone().then(() => {
            vertexBuffer?.destroy();
            uniformBuffer?.destroy();
        });
    });
</script>

<!--
    这个组件是一个“逻辑组件”，它不渲染任何可见的 HTML。
    它的全部工作都在 <script> 标签里完成：创建和管理 GPU 资源，
    并将自己的绘制能力注册到外部系统。
-->