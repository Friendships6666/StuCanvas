/*src/function/SDF/functionRenderer.ts*/
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

export async function initializeGpuResources ( device : GPUDevice , canvasFormat : GPUTextureFormat , sampleCount : number , formulas : any[] ,
                                               clipOffscreen : boolean ) : Promise<BatchRendererGpuResources | null> {
    if ( formulas.length === 0 ) return null;

    try {
        const uniformBuffer = device.createBuffer ( {
            label : `Batch Function Uniforms` ,
            // ✅ **核心修改**: 扩大缓冲区尺寸以容纳新数据
            // 原来是 48，现在加一个 f32 debug_mode。根据 WGSL 内存对齐规则，
            // 需扩展到 64 (48 + 16 for a padded vec4)
            size : 64,
            usage : GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST ,
        } );

        const functionData = new Float32Array ( formulas.flatMap ( f => [f.color.r , f.color.g , f.color.b , f.color.a] ) );
        const functionDataBuffer = device.createBuffer ( {
            label : `Batch Function Data Storage` ,
            size : Math.max ( functionData.byteLength , 16 ) ,
            usage : GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST ,
        } );
        device.queue.writeBuffer ( functionDataBuffer , 0 , functionData );

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
                targets : [
                    // ✅ **核心修改**: 定义两个渲染目标
                    // 目标0: 颜色 (写入到MSAA纹理或画布)
                    {
                        format : canvasFormat ,
                        blend : {
                            color : { srcFactor : 'one' , dstFactor : 'one-minus-src-alpha' , operation : 'add' },
                            alpha : { srcFactor : 'one' , dstFactor : 'one-minus-src-alpha' , operation : 'add' } ,
                        } ,
                    },
                    // 目标1: 调试值 (写入到 r32float 调试纹理)
                    {
                        format: 'r32float',
                    }
                ] ,
            } ,
            primitive : { topology : 'triangle-list' } ,
            multisample : { count : sampleCount } ,
        } );

        const bindGroup = device.createBindGroup ( {
            label : 'Batch Function Bind Group' ,
            layout : renderPipeline.getBindGroupLayout ( 0 ) ,
            entries : [{ binding : 0 , resource : { buffer : uniformBuffer } } , { binding : 1 , resource : { buffer : functionDataBuffer } } ,] ,
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

        return { renderPipeline , uniformBuffer , functionDataBuffer , bindGroup , renderable };

    } catch (e) {
        console.error ( "Failed to initialize batch function pipeline:" , e );
        return null;
    }
}