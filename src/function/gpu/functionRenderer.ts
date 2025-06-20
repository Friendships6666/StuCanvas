/*src/function/gpu/functionRenderer.ts*/
import type {Renderable} from '../../renderCore/renderer';
import {generateFragmentShader} from '../render/wgsl-generator';
import vertexShaderCode from './functionVertex.wgsl?raw';
import fragmentShaderTemplate from './functionFragment.wgsl?raw';

export interface BatchRendererGpuResources {
    renderPipeline : GPURenderPipeline;
    uniformBuffer : GPUBuffer;
    functionDataBuffer : GPUBuffer;
    bindGroup : GPUBindGroup;
    renderable : Renderable;
}

export async function initializeGpuResources ( device : GPUDevice , canvasFormat : GPUTextureFormat , sampleCount : number , formulas : any[] , // ✅ 新增: 接收 clipOffscreen 参数
                                               clipOffscreen : boolean ) : Promise<BatchRendererGpuResources | null> {
    if ( formulas.length === 0 ) return null;

    try {
        const uniformBuffer = device.createBuffer ( {
            label : `Batch Function Uniforms` , // ✅ **核心修改**: 扩大缓冲区尺寸以容纳新数据
            // 原来是 32 (vec4 + vec2)，现在加一个 float。根据 WGSL 内存对齐规则，
            // 需扩展到 48 (32 + 16 for a padded vec4)
            size : 48 ,
            usage : GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST ,
        } );

        const functionData = new Float32Array ( formulas.flatMap ( f => [f.color.r , f.color.g , f.color.b , f.color.a] ) );
        const functionDataBuffer = device.createBuffer ( {
            label : `Batch Function Data Storage` ,
            size : Math.max ( functionData.byteLength , 16 ) ,
            usage : GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST ,
        } );
        device.queue.writeBuffer ( functionDataBuffer , 0 , functionData );

        // ✅ 新增: 将 clipOffscreen 传递给着色器生成器
        const [definitions , evaluations] = generateFragmentShader ( formulas , clipOffscreen );

        const finalFragmentCode = fragmentShaderTemplate
            .replace ( '/*__WGSL_FUNCTION_DEFINITIONS__*/' , definitions )
            .replace ( '/*__WGSL_FUNCTION_EVALUATIONS__*/' , evaluations );

        const fragmentShaderModule = device.createShaderModule ( { code : finalFragmentCode } );
        const vertexShaderModule = device.createShaderModule ( { code : vertexShaderCode } );

        const renderPipeline = await device.createRenderPipelineAsync ( {
            label : 'Batch Function Pipeline' ,
            layout : 'auto' ,
            vertex : {
                module : vertexShaderModule ,
                entryPoint : 'vs_main'
            } ,
            fragment : {
                module : fragmentShaderModule ,
                entryPoint : 'fs_main' ,
                targets : [{
                    format : canvasFormat ,
                    blend : {
                        color : {
                            srcFactor : 'one' ,
                            dstFactor : 'one-minus-src-alpha' ,
                            operation : 'add'
                        } ,
                        alpha : {
                            srcFactor : 'one' ,
                            dstFactor : 'one-minus-src-alpha' ,
                            operation : 'add'
                        } ,
                    } ,
                }] ,
            } ,
            primitive : { topology : 'triangle-list' } ,
            multisample : { count : sampleCount } ,
        } );

        const bindGroup = device.createBindGroup ( {
            label : 'Batch Function Bind Group' ,
            layout : renderPipeline.getBindGroupLayout ( 0 ) ,
            entries : [{
                binding : 0 ,
                resource : { buffer : uniformBuffer }
            } , {
                binding : 1 ,
                resource : { buffer : functionDataBuffer }
            } ,] ,
        } );

        const renderable : Renderable = {
            draw : ( pass : GPURenderPassEncoder ) => {
                if ( !renderPipeline || !bindGroup || formulas.length === 0 ) return;
                pass.setPipeline ( renderPipeline );
                pass.setBindGroup ( 0 , bindGroup );
                pass.draw ( 6 );
            } ,
            layer : 3 ,
        };

        return {
            renderPipeline ,
            uniformBuffer ,
            functionDataBuffer ,
            bindGroup ,
            renderable
        };

    } catch (e) {
        console.error ( "Failed to initialize batch function pipeline:" , e );
        return null;
    }
}