<!--src/coordinate/rectangular/grid/GridLineRenderer.svelte-->
<script lang="ts">
    /// <reference types="@webgpu/types" />
    import {onMount , onDestroy} from 'svelte'; // ✅ 修正了这里的拼写错误
    import {hexToVec4} from '../../../utils/color-utils';
    import type {Renderable} from '../../../interaction/input/renderer';

    // --- Props ---
    export let register : Set<Renderable>;
    export let device : GPUDevice;
    export let canvasFormat : GPUTextureFormat;
    export let sampleCount : number;
    export let layer : number;
    export let color : string;
    export let vertices : Float32Array;

    // --- 内部 GPU 资源 ---
    let pipeline : GPURenderPipeline;
    let vertexBuffer : GPUBuffer;
    let uniformBuffer : GPUBuffer;
    let bindGroup : GPUBindGroup;

    /**
     * 初始化函数本身保持不变，它只负责创建资源。
     */
    function initialize () {
        const uniformBufferSize = 16;
        uniformBuffer = device.createBuffer ( {
            label : `GridLineBatch Uniform Buffer (Layer ${ layer })` ,
            size : uniformBufferSize ,
            usage : GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST ,
        } );

        const shaderModule = device.createShaderModule ( {
            label : `GridLineBatch Shader (Layer ${ layer })` ,
            code : `
                struct Uniforms { color: vec4f };
                @group(0) @binding(0) var<uniform> u: Uniforms;

                @vertex
                fn vs_main(@location(0) clip_pos: vec2f) -> @builtin(position) vec4f {
                    return vec4f(clip_pos, 0.0, 1.0);
                }

                @fragment
                fn fs_main() -> @location(0) vec4f {
                  return u.color;
                }
            ` ,
        } );

        const bindGroupLayout = device.createBindGroupLayout ( {
            entries : [{
                binding : 0 ,
                visibility : GPUShaderStage.FRAGMENT ,
                buffer : { type : 'uniform' }
            }]
        } );

        pipeline = device.createRenderPipeline ( {
            label : `GridLineBatch Pipeline (Layer ${ layer })` ,
            layout : device.createPipelineLayout ( { bindGroupLayouts : [bindGroupLayout] } ) ,
            vertex : {
                module : shaderModule ,
                entryPoint : 'vs_main' ,
                buffers : [{
                    arrayStride : 8 ,
                    attributes : [{
                        shaderLocation : 0 ,
                        offset : 0 ,
                        format : 'float32x2'
                    }]
                }]
            } ,
            fragment : {
                module : shaderModule ,
                entryPoint : 'fs_main' ,
                targets : [{ format : canvasFormat }]
            } ,
            primitive : { topology : 'triangle-list' } ,
            multisample : { count : sampleCount } ,
        } );

        bindGroup = device.createBindGroup ( {
            label : `GridLineBatch BindGroup (Layer ${ layer })` ,
            layout : pipeline.getBindGroupLayout ( 0 ) ,
            entries : [{
                binding : 0 ,
                resource : { buffer : uniformBuffer }
            }] ,
        } );
    }

    // 在这里统一管理初始化和更新逻辑
    $: if ( device && vertices ) {
        // 1. 如果 pipeline 不存在，说明是第一次运行，立即进行初始化。
        if ( !pipeline ) {
            initialize ();
        }

        // 2. 管理并更新顶点缓冲区 (Buffer)
        if ( !vertexBuffer || vertices.byteLength > vertexBuffer.size ) {
            if ( vertexBuffer ) {
                vertexBuffer.destroy ();
            }
            const newCapacity = Math.max ( 64 , vertices.byteLength * 1.5 );
            vertexBuffer = device.createBuffer ( {
                label : `GridLineBatch Vertex Buffer (Layer ${ layer })` ,
                size : newCapacity ,
                usage : GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST ,
            } );
        }

        // 3. 将最新数据写入 Buffer
        device.queue.writeBuffer ( vertexBuffer , 0 , vertices );
        device.queue.writeBuffer ( uniformBuffer , 0 , hexToVec4 ( color ) );
    }

    /**
     * draw 函数保持不变，它依然纯粹且高效。
     */
    function draw ( pass : GPURenderPassEncoder ) {
        if ( !pipeline || !vertexBuffer || vertices.length === 0 ) return;

        pass.setPipeline ( pipeline );
        pass.setVertexBuffer ( 0 , vertexBuffer );
        pass.setBindGroup ( 0 , bindGroup );
        pass.draw ( vertices.length / 2 );
    }

    onMount ( () => {
        const self : Renderable = {
            draw ,
            layer
        };
        register.add ( self );
        return () => register.delete ( self );
    } );

    onDestroy ( () => {
        // onDestroy 逻辑保持不变
        device?.queue.onSubmittedWorkDone ().then ( () => {
            vertexBuffer?.destroy ();
            uniformBuffer?.destroy ();
        } );
    } );
</script>