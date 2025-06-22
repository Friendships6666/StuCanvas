import type { DrawableFormula } from '../../coordinate/rectangular/use/use-formulas';
import { translateJsExpressionToWgsl } from '../formula/wgsl-translator';

export function generateMaskShader(formulas: DrawableFormula[]): string {
    if (!formulas || formulas.length === 0) {
        return "";
    }

    const functionSolvers = formulas.map((f, i) => {
        try {
            const expression = f.wgsl_expression;
            const match = expression.match(/^\s*\(\s*y\s*\)\s*-\s*\((.*)\)\s*$/);

            if (match && match[1]) {
                const fxString = match[1];
                const yExpression = translateJsExpressionToWgsl(fxString);
                return `
        case ${i}u: {
            y = ${yExpression};
            break;
        }`;
            }
            return `        case ${i}u: { return; }`;
        } catch (e) {
            return `        case ${i}u: { return; }`;
        }
    }).join('');

    return `
struct Uniforms {
    view: vec4<f32>,
    canvas: vec2<f32>,
    params: vec4<f32>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var validationMask: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let point_index = global_id.x;
    let total_points = u32(uniforms.params.z);
    if (point_index >= total_points) { return; }

    let formula_index = u32(uniforms.params.w);
    let x_range = uniforms.params.y - uniforms.params.x;
    let x = uniforms.params.x + (f32(point_index) / f32(total_points - 1u)) * x_range;
    var y: f32 = 0.0;

    switch (formula_index) {
${functionSolvers}
        default: { return; }
    }

    let aspect = uniforms.view.w;
    let zoom = uniforms.view.z;
    let view_x = uniforms.view.x;
    let view_y = uniforms.view.y;
    let ndc_x = (x - view_x) * zoom / aspect;
    let ndc_y = (y - view_y) * zoom;

    if (abs(ndc_x) > 1.0 || abs(ndc_y) > 1.0) { return; }

    let pixel_coords = vec2<i32>(
        i32(floor((ndc_x * 0.5 + 0.5) * uniforms.canvas.x)),
        i32(floor((-ndc_y * 0.5 + 0.5) * uniforms.canvas.y))
    );

    textureStore(validationMask, pixel_coords, vec4<f32>(1.0, 0.0, 0.0, 1.0));
}
    `;
}