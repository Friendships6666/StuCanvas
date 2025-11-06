#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <tuple>
#include <utility>
#include <array>

// ===============================================================
// === Emscripten 绑定库 ===
// ===============================================================
#include <emscripten/bind.h>

// ===============================================================
// === 核心依赖库 (TBB 和 xsimd) ===
// ===============================================================
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/combinable.h>
#include <xsimd/xsimd.hpp>

namespace xs = xsimd;

// ===============================================================
// === 数据结构定义 ===
// ===============================================================
struct Vec2 { double x; double y; };
struct PointData { Vec2 position; unsigned int function_index; };
struct Uniforms { Vec2 screen_dimensions; double zoom; Vec2 offset; };

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline))
#endif

// ===============================================================
// === 核心辅助与计算函数 (已修改) ===
// ===============================================================
FORCE_INLINE const xs::batch<double>& get_index_vec() {
    using batch_type = xs::batch<double>;
    static const auto index_vec = [] {
        constexpr std::size_t batch_size = batch_type::size;
        alignas(batch_type::arch_type::alignment()) std::array<double, batch_size> indices{};
        for (std::size_t i = 0; i < batch_size; ++i) {
            indices[i] = static_cast<double>(i);
        }
        return xs::load_aligned(indices.data());
    }();
    return index_vec;
}

// === [修改] 新的坐标转换逻辑：增量式计算 ===

// 将屏幕坐标转换为世界坐标（标量版本）
FORCE_INLINE Vec2 screen_to_world_inline(
    const Vec2& screen_pos,
    const Vec2& world_origin,
    double world_per_pixel_x,
    double world_per_pixel_y
) {
    return {
        world_origin.x + screen_pos.x * world_per_pixel_x,
        world_origin.y + screen_pos.y * world_per_pixel_y
    };
}

// 将屏幕坐标转换为世界坐标（SIMD批处理版本）
FORCE_INLINE std::pair<xs::batch<double>, xs::batch<double>>
screen_to_world_batch(
    const xs::batch<double>& sx, double sy,
    const Vec2& world_origin,
    double world_per_pixel_x,
    double world_per_pixel_y
) {
    auto wx = world_origin.x + sx * world_per_pixel_x;
    auto wy = world_origin.y + sy * world_per_pixel_y;
    return { wx, wy };
}


FORCE_INLINE Vec2 get_intersection_point_inline(const Vec2& p1, const Vec2& p2, double v1, double v2) {
    double t = -v1 / (v2 - v1);
    return {p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y)};
}

constexpr unsigned int TILE_W = 128;
constexpr unsigned int TILE_H = 64;

struct ThreadCacheForTiling {
    std::vector<double> top_row_vals;
    std::vector<double> bot_row_vals;
    std::vector<PointData> point_buffer;

    ThreadCacheForTiling() {
        top_row_vals.resize(TILE_W + 1);
        bot_row_vals.resize(TILE_W + 1);
        point_buffer.reserve(1024);
    }
};

template<typename F_Scalar, typename F_Batch>
void process_tile(
    // === [修改] 接收新的坐标参数和原始偏移量 ===
    const Vec2& world_origin, double world_per_pixel_x, double world_per_pixel_y, const Vec2& view_offset,
    const F_Scalar& scalar_func, const F_Batch& batch_func, unsigned int func_idx,
    unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
    ThreadCacheForTiling& cache,
    std::vector<PointData>& final_thread_vector)
{
    const unsigned int tile_w = x_end - x_start;
    auto& top_row_vals = cache.top_row_vals;
    auto& bot_row_vals = cache.bot_row_vals;
    auto& point_buffer = cache.point_buffer;

    using batch_type = xs::batch<double>;
    constexpr std::size_t batch_size = batch_type::size;

    for (unsigned int x = x_start; x <= x_end; ++x) {
        // === [修改] 使用新的坐标转换函数 ===
        top_row_vals[x - x_start] = scalar_func(screen_to_world_inline({(double)x, (double)y_start}, world_origin, world_per_pixel_x, world_per_pixel_y));
    }

    for (unsigned int y = y_start; y < y_end; ++y) {
        const std::size_t vec_end = tile_w - (tile_w % batch_size);
        for (std::size_t x_offset = 0; x_offset < vec_end; x_offset += batch_size) {
            batch_type sx = get_index_vec() + static_cast<double>(x_start + x_offset);
            // === [修改] 使用新的坐标转换函数 ===
            auto [wx, wy] = screen_to_world_batch(sx, (double)y + 1.0, world_origin, world_per_pixel_x, world_per_pixel_y);
            xs::store_unaligned(&bot_row_vals[x_offset], batch_func(wx, wy));
        }
        for (std::size_t x_offset = vec_end; x_offset <= tile_w; ++x_offset) {
            // === [修改] 使用新的坐标转换函数 ===
            bot_row_vals[x_offset] = scalar_func(screen_to_world_inline({(double)(x_start + x_offset), (double)y + 1.0}, world_origin, world_per_pixel_x, world_per_pixel_y));
        }

        point_buffer.clear();
        for (unsigned int x_offset = 0; x_offset < tile_w; ++x_offset) {
            double val_tl = top_row_vals[x_offset];
            double val_tr = top_row_vals[x_offset + 1];
            double val_bl = bot_row_vals[x_offset];
            double sign_tl = (val_tl > 0.0) - (val_tl < 0.0);
            if (std::isnan(val_tl) || std::isnan(val_tr) || std::isnan(val_bl)) {
                continue;
            }
            if (((val_tr > 0.0) - (val_tr < 0.0)) != sign_tl || ((val_bl > 0.0) - (val_bl < 0.0)) != sign_tl) {
                constexpr double sub_cell_step = 0.5;
                for (int ly = 0; ly < 2; ++ly) {
                    for (int lx = 0; lx < 2; ++lx) {
                        Vec2 sub_tl_scr = {(double)(x_start + x_offset) + (double)lx * sub_cell_step, (double)y + (double)ly * sub_cell_step};
                        // === [修改] 使用新的坐标转换函数 ===
                        Vec2 p_tl = screen_to_world_inline(sub_tl_scr, world_origin, world_per_pixel_x, world_per_pixel_y);
                        Vec2 p_tr = screen_to_world_inline({sub_tl_scr.x + sub_cell_step, sub_tl_scr.y}, world_origin, world_per_pixel_x, world_per_pixel_y);
                        Vec2 p_bl = screen_to_world_inline({sub_tl_scr.x, sub_tl_scr.y + sub_cell_step}, world_origin, world_per_pixel_x, world_per_pixel_y);
                        double sub_val_tl = scalar_func(p_tl);
                        double sub_val_tr = scalar_func(p_tr);
                        double sub_val_bl = scalar_func(p_bl);

                        if (std::signbit(sub_val_tl) != std::signbit(sub_val_tr)) {
                            Vec2 intersection_point = get_intersection_point_inline(p_tl, p_tr, sub_val_tl, sub_val_tr);
                            // === [保留的优化] 减去原始偏移量以存储相对坐标 ===
                            intersection_point.x -= view_offset.x;
                            intersection_point.y -= view_offset.y;
                            point_buffer.push_back({intersection_point, func_idx});
                        }

                        if (std::signbit(sub_val_tl) != std::signbit(sub_val_bl)) {
                            Vec2 intersection_point = get_intersection_point_inline(p_tl, p_bl, sub_val_tl, sub_val_bl);
                            // === [保留的优化] 减去原始偏移量以存储相对坐标 ===
                            intersection_point.x -= view_offset.x;
                            intersection_point.y -= view_offset.y;
                            point_buffer.push_back({intersection_point, func_idx});
                        }
                    }
                }
            }
        }
        if (!point_buffer.empty()) {
            final_thread_vector.insert(final_thread_vector.end(), point_buffer.begin(), point_buffer.end());
        }
        std::swap(top_row_vals, bot_row_vals);
    }
}

template<typename... Fs, typename... Fbs, std::size_t... Is>
void process_tile_for_all_funcs(
    // === [修改] 传递新的坐标参数 ===
    const Vec2& world_origin, double world_per_pixel_x, double world_per_pixel_y, const Vec2& view_offset,
    const std::tuple<Fs...>& scalar_funcs, const std::tuple<Fbs...>& batch_funcs,
    unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
    ThreadCacheForTiling& cache, std::vector<PointData>& final_thread_vector, std::index_sequence<Is...>)
{
    (process_tile(world_origin, world_per_pixel_x, world_per_pixel_y, view_offset, std::get<Is>(scalar_funcs), std::get<Is>(batch_funcs), Is, x_start, x_end, y_start, y_end, cache, final_thread_vector), ...);
}

template<typename... Funcs, typename... BatchFuncs>
std::vector<PointData> generatePointCloud_Tiled(
    // === [修改] 接收新的坐标参数 ===
    unsigned int screen_w, unsigned int screen_h,
    const Vec2& world_origin, double world_per_pixel_x, double world_per_pixel_y, const Vec2& view_offset,
    const std::tuple<Funcs...>& scalar_functions,
    const std::tuple<BatchFuncs...>& batch_functions)
{
    const unsigned int num_tiles_w = (screen_w + TILE_W - 1) / TILE_W;
    const unsigned int num_tiles_h = (screen_h + TILE_H - 1) / TILE_H;
    const unsigned int total_tiles = num_tiles_w * num_tiles_h;

    using PointVector = std::vector<PointData>;
    oneapi::tbb::combinable<PointVector> thread_local_points;
    oneapi::tbb::combinable<ThreadCacheForTiling> thread_local_caches;

    oneapi::tbb::parallel_for(
        oneapi::tbb::blocked_range<unsigned int>(0, total_tiles),
        [&](const oneapi::tbb::blocked_range<unsigned int>& r) {
            PointVector& local_points = thread_local_points.local();
            ThreadCacheForTiling& cache = thread_local_caches.local();
            for (unsigned int tile_idx = r.begin(); tile_idx < r.end(); ++tile_idx) {
                unsigned int tile_y = tile_idx / num_tiles_w;
                unsigned int tile_x = tile_idx % num_tiles_w;
                unsigned int x_start = tile_x * TILE_W;
                unsigned int y_start = tile_y * TILE_H;
                unsigned int x_end = std::min(x_start + TILE_W, screen_w);
                unsigned int y_end = std::min(y_start + TILE_H, screen_h);
                // === [修改] 调用更新后的函数 ===
                process_tile_for_all_funcs(world_origin, world_per_pixel_x, world_per_pixel_y, view_offset,
                                           scalar_functions, batch_functions,
                                           x_start, x_end, y_start, y_end,
                                           cache, local_points, std::index_sequence_for<Funcs...>{});
            }
        }
    );

    std::vector<PointData> final_points;
    size_t total_points = 0;
    thread_local_points.combine_each([&](const PointVector& v) { total_points += v.size(); });
    final_points.reserve(total_points);
    thread_local_points.combine_each([&](const PointVector& v) { final_points.insert(final_points.end(), v.begin(), v.end()); });
    return final_points;
}

// ===============================================================
// === WASM 导出函数 (已修改) ===
// ===============================================================
std::vector<PointData> generatePointsWASM(
    double screen_width, double screen_height,
    double zoom, double offset_x, double offset_y,
    int thread_count)
{
    tbb::global_control control(tbb::global_control::max_allowed_parallelism, thread_count);

    auto scalar_funcs_tuple = std::make_tuple(
        [](Vec2 p) { return std::cos(p.x) + std::sin(p.y) - 0.1; }
    );
    auto batch_funcs_tuple = std::make_tuple(
        [](const xs::batch<double>& x, const xs::batch<double>& y) {
            return xs::cos(x) + xs::sin(y) - 0.1;
        }
    );

    Uniforms uniforms = {{screen_width, screen_height}, zoom, {offset_x, offset_y}};

    // === [修改] 在这里一次性计算出坐标转换所需的所有参数 ===
    double aspect_ratio = uniforms.screen_dimensions.x / uniforms.screen_dimensions.y;

    // 1. 计算屏幕(0,0)点对应的世界坐标，作为世界坐标系的原点
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);
    Vec2 world_origin = {
        (centered_x_0 / uniforms.zoom) + uniforms.offset.x,
        (centered_y_0 / uniforms.zoom) + uniforms.offset.y
    };

    // 2. 计算每个屏幕像素在世界坐标系中的增量
    double world_per_pixel_x = (2.0 * aspect_ratio) / (uniforms.zoom * uniforms.screen_dimensions.x);
    double world_per_pixel_y = -2.0 / (uniforms.zoom * uniforms.screen_dimensions.y);

    // === [修改] 调用更新后的点云生成函数 ===
    return generatePointCloud_Tiled(
        (unsigned int)screen_width, (unsigned int)screen_height,
        world_origin, world_per_pixel_x, world_per_pixel_y, uniforms.offset,
        scalar_funcs_tuple, batch_funcs_tuple
    );
}


// ===============================================================
// === Emscripten 绑定 (无变动) ===
// ===============================================================
EMSCRIPTEN_BINDINGS(my_point_cloud_module) {
    emscripten::value_object<Vec2>("Vec2")
        .field("x", &Vec2::x)
        .field("y", &Vec2::y);

    emscripten::value_object<PointData>("PointData")
        .field("position", &PointData::position)
        .field("function_index", &PointData::function_index);

    emscripten::register_vector<PointData>("VectorPointData");

    emscripten::function("generatePoints", &generatePointsWASM);
}