<!--src/coordinate/rectangular/label-->
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
                struct Uniforms {
                    color: vec4f
                };
                @group(0) @binding(0) var<uniform> u: Uniforms;

                // ✅ *** 核心修复 1/2 ***
                // 定义一个包含两个输出的结构体，以匹配渲染通道的期望。
                struct FragmentOutput {
                    @location(0) color: vec4f,
                    // 我们不使用这个输出，但必须声明它以保证管线兼容性。
                    @location(1) dummy_debug: f32,
                };

                @vertex
                fn vs_main(@location(0) clip_pos: vec2f) -> @builtin(position) vec4f {
                    return vec4f(clip_pos, 0.0, 1.0);
                }

                @fragment
                fn fs_main() -> FragmentOutput {
                  var output: FragmentOutput;
                  output.color = u.color;
                  // 必须为所有输出赋值。我们写入一个无意义的默认值。
                  output.dummy_debug = 0.0;
                  return output;
                }
            `,
        });

        // 创建绑定组布局，描述着色器需要哪些资源
        const bindGroupLayout = device.createBindGroupLayout({
            entries: [{
                binding: 0,
                visibility: GPUShaderStage.FRAGMENT,
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
                // ✅ *** 核心修复 2/2 ***
                // 明确告诉管线它将输出到两个目标，以匹配主渲染通道的配置。
                targets: [
                    { format: canvasFormat }, // 目标0: 颜色
                    { format: 'r32float' }    // 目标1: 调试值
                ]
            },
            primitive: {
                topology: 'line-list'
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
    $: if (device && vertices) {
        if (!pipeline) {
            initialize();
        }

        if (!vertexBuffer || vertices.byteLength > vertexBuffer.size) {
            vertexBuffer?.destroy();
            const newCapacity = Math.max(64, vertices.byteLength * 1.5);
            vertexBuffer = device.createBuffer({
                label: `GridLine Vertex Buffer (Layer ${layer})`,
                size: newCapacity,
                usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
            });
        }

        device.queue.writeBuffer(vertexBuffer, 0, vertices);
        device.queue.writeBuffer(uniformBuffer, 0, hexToVec4(color));
    }

    // --- Renderable 接口的实现 ---
    function draw(pass: GPURenderPassEncoder) {
        if (!pipeline || !vertexBuffer || vertices.length === 0) return;

        pass.setPipeline(pipeline);
        pass.setVertexBuffer(0, vertexBuffer);
        pass.setBindGroup(0, bindGroup);
        pass.draw(vertices.length / 2);
    }

    // --- Svelte 生命周期钩子 ---
    onMount(() => {
        const self: Renderable = {
            draw,
            layer
        };
        register.add(self);
        return () => {
            register.delete(self);
        };
    });

    onDestroy(() => {
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