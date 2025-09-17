// ===============================================================
// === 数据结构和资源绑定 ===
// ===============================================================

struct Uniforms {
  screen_dimensions: vec2<f32>,
  zoom: f32,
  offset: vec2<f32>,
  dispatch_width: u32,
};
struct CoarseCells { data: array<vec2<u32>> };
struct PointCloud { data: array<vec2<f32>> };
struct IndirectParams {
    compute_x: atomic<u32>,
    compute_y: atomic<u32>,
    compute_z: atomic<u32>,
    draw_vertex_count: atomic<u32>,
    draw_instance_count: atomic<u32>,
    draw_first_vertex: atomic<u32>,
    draw_first_instance: atomic<u32>,
};

@group(0) @binding(0) var<uniform> c_params: Uniforms;
@group(0) @binding(1) var<storage, read_write> counter_pass1: atomic<u32>;
@group(0) @binding(2) var<storage, read_write> active_cells_pass1: CoarseCells;
@group(0) @binding(3) var<storage, read_write> indirect_params_staging: IndirectParams;
@group(0) @binding(4) var<storage, read_write> point_counter: atomic<u32>;
@group(0) @binding(5) var<storage, read_write> point_cloud_buffer_writable: PointCloud;

@group(1) @binding(0) var<uniform> r_params: Uniforms;
@group(1) @binding(1) var<storage, read> point_cloud_buffer_readonly: PointCloud;

// ===============================================================
// === 辅助函数 (保持不变) ===
// ... (此处省略) ...
// ===============================================================
fn screen_to_world(pixel_coords: vec2<f32>, uniforms: Uniforms) -> vec2<f32> {
  let screen_dims = uniforms.screen_dimensions;
  let normalized = pixel_coords / screen_dims;
  var centered = normalized - 0.5;
  centered.x = centered.x * (screen_dims.x / screen_dims.y);
  centered.y = -centered.y;
  return (centered / uniforms.zoom) + uniforms.offset;
}



fn F(p: vec2<f32>) -> f32 {
    return p.y-sin(p.x)*cos(p.y) +p.x;
}




fn world_to_clip(world_pos: vec2<f32>, uniforms: Uniforms) -> vec4<f32> {
    let screen_dims = uniforms.screen_dimensions;
    var temp_pos = (world_pos - uniforms.offset) * uniforms.zoom;
    temp_pos.x = temp_pos.x / (screen_dims.x / screen_dims.y);
    return vec4<f32>(temp_pos * 2.0, 0.0, 1.0);
}
fn world_to_screen(world_pos: vec2<f32>, uniforms: Uniforms) -> vec2<f32> {
    let screen_dims = uniforms.screen_dimensions;
    var temp_pos = (world_pos - uniforms.offset) * uniforms.zoom;
    temp_pos.x = temp_pos.x / (screen_dims.x / screen_dims.y);
    var normalized = temp_pos + 0.5;
    return vec2<f32>(normalized.x * screen_dims.x, (1.0 - normalized.y) * screen_dims.y);
}
fn get_intersection_point(p1: vec2<f32>, p2: vec2<f32>, v1: f32, v2: f32) -> vec2<f32> {
    let t = -v1 / (v2 - v1);
    return mix(p1, p2, t);
}


// ===============================================================
// === 计算着色器 Pass 1, 2, 3 (保持不变) ===
// ... (此处省略) ...
// ===============================================================
var<workgroup> value_cache: array<array<f32, 17>, 17>;
@compute @workgroup_size(16, 16)
fn compute_pass_1(@builtin(global_invocation_id) global_id: vec3<u32>, @builtin(local_invocation_id) local_id: vec3<u32>) {
    let screen_dims = vec2<u32>(u32(c_params.screen_dimensions.x), u32(c_params.screen_dimensions.y));
    var screen_pos = vec2<f32>(global_id.xy);
    value_cache[local_id.y][local_id.x] = F(screen_to_world(screen_pos, c_params));
    if (local_id.x == 15u) {
        screen_pos = vec2<f32>(f32(global_id.x + 1u), f32(global_id.y));
        value_cache[local_id.y][16] = F(screen_to_world(screen_pos, c_params));
    }
    if (local_id.y == 15u) {
        screen_pos = vec2<f32>(f32(global_id.x), f32(global_id.y + 1u));
        value_cache[16][local_id.x] = F(screen_to_world(screen_pos, c_params));
    }
    if (local_id.x == 15u && local_id.y == 15u) {
        screen_pos = vec2<f32>(f32(global_id.x + 1u), f32(global_id.y + 1u));
        value_cache[16][16] = F(screen_to_world(screen_pos, c_params));
    }
    workgroupBarrier();
    if (global_id.x >= screen_dims.x || global_id.y >= screen_dims.y) { return; }

    let val_tl = value_cache[local_id.y][local_id.x];
    let val_tr = value_cache[local_id.y][local_id.x + 1u];
    let val_bl = value_cache[local_id.y + 1u][local_id.x];
    let val_br = value_cache[local_id.y + 1u][local_id.x + 1u];
    // ======================= 核心修正点 =======================
    // 步骤 3: 检查是否有任何一个值为 NaN。
    //         NaN 的特性是它不等于任何值，包括它自己。
    if (val_tl + 1.0 == val_tl || val_tr + 1.0 == val_tr || val_bl + 1.0 == val_bl || val_br + 1.0 == val_br) {
        // 如果网格的任何一个角落是无穷大，则我们认为这个网格是不稳定的，
        // 不应该在这里生成任何几何体。直接退出。
        return;
    }
    let sign_tl = sign(val_tl);
    if (sign(val_tr) != sign_tl || sign(val_bl) != sign_tl || sign(val_br) != sign_tl) {
        let index = atomicAdd(&counter_pass1, 1u);
        active_cells_pass1.data[index] = global_id.xy;
    }
}
@compute @workgroup_size(1, 1)
fn prepare_compute_indirect() {
    let compute_count = atomicLoad(&counter_pass1);
    let dispatch_width = c_params.dispatch_width;
    let dispatch_x = dispatch_width;
    let dispatch_y = (compute_count + dispatch_width - 1u) / dispatch_width;
    atomicStore(&indirect_params_staging.compute_x, dispatch_x);
    atomicStore(&indirect_params_staging.compute_y, dispatch_y);
    atomicStore(&indirect_params_staging.compute_z, 1u);
}
@compute @workgroup_size(2, 2)
fn compute_pass_3(@builtin(workgroup_id) workgroup_id: vec3<u32>, @builtin(local_invocation_id) local_id: vec3<u32>) {
    let cell_index = workgroup_id.y * c_params.dispatch_width + workgroup_id.x;
    let total_active_cells = atomicLoad(&counter_pass1);
    if (cell_index >= total_active_cells) { return; }
    let coarse_coord = active_cells_pass1.data[cell_index];
    let sub_cell_step = 1.0 / 2.0;
    let sub_cell_tl_screen = vec2<f32>(coarse_coord) + vec2<f32>(local_id.xy) * sub_cell_step;
    let p_tl = screen_to_world(sub_cell_tl_screen, c_params);
    let p_tr = screen_to_world(sub_cell_tl_screen + vec2<f32>(sub_cell_step, 0.0), c_params);
    let p_bl = screen_to_world(sub_cell_tl_screen + vec2<f32>(0.0, sub_cell_step), c_params);
    let val_tl = F(p_tl);
    let val_tr = F(p_tr);
    let val_bl = F(p_bl);
    var case_index = 0u;
    if (val_tl > 0.0) { case_index = case_index | 1u; }
    if (val_tr > 0.0) { case_index = case_index | 2u; }
    if (F(p_bl) > 0.0) { case_index = case_index | 8u; }
    if (F(p_tr + vec2<f32>(0.0, sub_cell_step)) > 0.0) { case_index = case_index | 4u; }
    if ((case_index & 1u) != (case_index & 2u) >> 1u) {
        let point_pos = get_intersection_point(p_tl, p_tr, val_tl, val_tr);
        let index = atomicAdd(&point_counter, 1u);
        point_cloud_buffer_writable.data[index] = point_pos;
    }
    if ((case_index & 1u) != (case_index & 8u) >> 3u) {
        let point_pos = get_intersection_point(p_tl, p_bl, val_tl, val_bl);
        let index = atomicAdd(&point_counter, 1u);
        point_cloud_buffer_writable.data[index] = point_pos;
    }
}

// ===============================================================
// === 渲染准备 Pass & 渲染着色器 (已修改) ===
// ===============================================================
@compute @workgroup_size(1, 1)
fn prepare_draw_indirect() {
    let point_count = atomicLoad(&point_counter);
    atomicStore(&indirect_params_staging.draw_vertex_count, point_count * 3u);
    atomicStore(&indirect_params_staging.draw_instance_count, 1u);
    atomicStore(&indirect_params_staging.draw_first_vertex, 0u);
    atomicStore(&indirect_params_staging.draw_first_instance, 0u);
}

struct VertexOutput {
    @builtin(position) clip_position: vec4<f32>,
    @location(0) world_pos: vec2<f32>,
    @location(1) point_center_world: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> VertexOutput {
    let point_index = in_vertex_index / 3u;
    let vertex_id = in_vertex_index % 3u;
    let point_center_world = point_cloud_buffer_readonly.data[point_index];
    let pixel_world_size = 1.0 / (r_params.zoom * r_params.screen_dimensions.y);
    let triangle_radius = 5.0 * pixel_world_size; // 保持足够大的三角形
    var offsets = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 1.0) * 1.15,
        vec2<f32>(-1.0, -0.58),
        vec2<f32>(1.0, -0.58)
    );
    let offset_world = offsets[vertex_id] * triangle_radius;
    let corner_world_pos = point_center_world + offset_world;
    var out: VertexOutput;
    out.clip_position = world_to_clip(corner_world_pos, r_params);
    out.world_pos = corner_world_pos;
    out.point_center_world = point_center_world;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let frag_screen_coord = world_to_screen(in.world_pos, r_params);
    let center_screen_coord = world_to_screen(in.point_center_world, r_params);
    let dist_vec = frag_screen_coord - center_screen_coord;
    let distance = length(dist_vec);

    // ======================= FIX IS HERE =======================
    // 移除 0.5 的实体核心，让渐变从 0.0 (中心点) 开始
    let alpha = 1.0 - smoothstep(0.0, 3, distance);
    // ===============================================================

    if (alpha < 0.01) {
        discard;
    }

    return vec4<f32>(1.0, 0.8, 0.2, alpha);
}