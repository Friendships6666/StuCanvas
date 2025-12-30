// --- 文件路径: src/plot/plotImplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/functions/lerp.h"     // 包含 world_to_clip_store
#include "../../include/functions/functions.h"
#include <stack>
#include <algorithm>
#include <cmath>

// 任务结构体，用于四叉树细分
struct QuadtreeTask {
    double world_x, world_y; // 区块左上角的世界坐标
    double world_w, world_h; // 区块在世界坐标系下的宽高
};

// 构造函数
ThreadCacheForTiling::ThreadCacheForTiling() {
    top_row_vals.resize(TILE_W + 1);
    bot_row_vals.resize(TILE_W + 1);
}

// 内部工作函数：在叶子节点(Tile)内进行像素级 Marching Squares
// 注意：输入仍然是 World Space，计算过程保持 Double 精度
void draw_points_in_tile(
    oneapi::tbb::concurrent_vector<PointData>& concurrent_points,
    const Vec2& world_origin, double wppx, double wppy,
    const AlignedVector<RPNToken>& rpn_program, const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx, unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
    ThreadCacheForTiling& cache,
    const NDCMap& ndc_map // ★★★ 传入映射表
) {
    const unsigned int tile_w = x_end - x_start;
    if (tile_w > TILE_W || tile_w == 0) return;

    auto& top_row_vals = cache.top_row_vals;
    auto& bot_row_vals = cache.bot_row_vals;

    // 1. 预计算顶行值 (Double)
    for (unsigned int x = x_start; x <= x_end; ++x) {
        top_row_vals[x - x_start] = evaluate_rpn<double>(rpn_program, world_origin.x + x * wppx, world_origin.y + y_start * wppy);
    }

    // 2. 逐行扫描
    for (unsigned int y = y_start; y < y_end; ++y) {
        // SIMD 批量计算底行值
        const std::size_t vec_end = tile_w - (tile_w % batch_type::size);
        for (std::size_t x_off = 0; x_off < vec_end; x_off += batch_type::size) {
            batch_type sx = get_index_vec() + static_cast<double>(x_start + x_off);
            auto [wx, wy] = screen_to_world_batch(sx, (double)y + 1.0, world_origin, wppx, wppy);
            xs::store_aligned(&bot_row_vals[x_off], evaluate_rpn<batch_type>(rpn_program, wx, wy));
        }
        // 标量处理尾部
        for (std::size_t x_off = vec_end; x_off <= tile_w; ++x_off) {
            Vec2 world_pos = screen_to_world_inline({(double)(x_start + x_off), (double)y + 1.0}, world_origin, wppx, wppy);
            bot_row_vals[x_off] = evaluate_rpn<double>(rpn_program, world_pos.x, world_pos.y);
        }

        // Marching Squares 求交
        for (unsigned int x_off = 0; x_off < tile_w; ++x_off) {
            double tl = top_row_vals[x_off], tr = top_row_vals[x_off + 1], bl = bot_row_vals[x_off];

            // 过滤无效值
            if (!std::isfinite(tl) || !std::isfinite(tr) || !std::isfinite(bl)) continue;

            // 简单拓扑检查 (同号跳过)
            double sign_tl = (tl > 0.0) - (tl < 0.0);
            if (((tr > 0.0) - (tr < 0.0)) == sign_tl && ((bl > 0.0) - (bl < 0.0)) == sign_tl) continue;

            constexpr double step = 0.5;
            Vec2 intersection{}; // 这里的 intersection 是 Double 精度的世界坐标

            // 检查 Tile 内的 2x2 子网格
            for (int ly = 0; ly < 2; ++ly) for (int lx = 0; lx < 2; ++lx) {
                Vec2 s_tl_scr = {(double)(x_start + x_off) + lx * step, (double)y + ly * step};

                // 计算三角形顶点 (World Space)
                Vec2 p_tl = screen_to_world_inline(s_tl_scr, world_origin, wppx, wppy);
                Vec2 p_tr = screen_to_world_inline({s_tl_scr.x + step, s_tl_scr.y}, world_origin, wppx, wppy);
                Vec2 p_bl = screen_to_world_inline({s_tl_scr.x, s_tl_scr.y + step}, world_origin, wppx, wppy);

                // 求值
                double v_tl = evaluate_rpn<double>(rpn_program, p_tl.x, p_tl.y);
                double v_tr = evaluate_rpn<double>(rpn_program, p_tr.x, p_tr.y);
                double v_bl = evaluate_rpn<double>(rpn_program, p_bl.x, p_bl.y);

                if (!std::isfinite(v_tl) || !std::isfinite(v_tr) || !std::isfinite(v_bl)) continue;

                // =========================================================
                // 线性插值求交 (World Space)
                // =========================================================
                if (try_get_intersection_point(intersection, p_tl, p_tr, v_tl, v_tr, rpn_program_check)) {
                    // ★★★ 核心修改：Double World -> Float Clip ★★★
                    PointData pd;
                    world_to_clip_store(pd, intersection.x, intersection.y, ndc_map, func_idx);
                    concurrent_points.push_back(pd);
                }

                if (try_get_intersection_point(intersection, p_tl, p_bl, v_tl, v_bl, rpn_program_check)) {
                    // ★★★ 核心修改：Double World -> Float Clip ★★★
                    PointData pd;
                    world_to_clip_store(pd, intersection.x, intersection.y, ndc_map, func_idx);
                    concurrent_points.push_back(pd);
                }
            }
        }
        // 交换行缓存，准备下一行
        std::swap(top_row_vals, bot_row_vals);
    }
}

// 主生产者函数
void process_implicit_adaptive(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    double offset_x, double offset_y,
    const NDCMap& ndc_map // ★★★ 接收映射参数
) {
    // 1. 创建并发向量
    oneapi::tbb::concurrent_vector<PointData> concurrent_points;

    // 线程局部缓存
    oneapi::tbb::combinable<ThreadCacheForTiling> thread_local_caches;

    // 2. 四叉树细分逻辑 (World Space)
    // 这里的逻辑依然在世界坐标系进行，确保区间算术的正确性
    std::stack<QuadtreeTask> tasks;
    std::vector<QuadtreeTask> leaf_nodes;

    // 初始根节点：整个屏幕视口 (World Space)
    tasks.push({ world_origin.x, world_origin.y, screen_width * wppx, screen_height * wppy });

    const double min_pixel_width = 10 * wppx;
    const double min_pixel_height = 10 * std::abs(wppy);

    alignas(batch_type::arch_type::alignment()) double min_x[BATCH_SIZE], max_x[BATCH_SIZE];
    alignas(batch_type::arch_type::alignment()) double min_y[BATCH_SIZE], max_y[BATCH_SIZE];
    QuadtreeTask task_lanes[BATCH_SIZE];

    while (true) {
        // --- SIMD 批量区间检测 ---
        while (tasks.size() >= BATCH_SIZE) {
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                task_lanes[i] = tasks.top(); tasks.pop();
                min_x[i] = task_lanes[i].world_x;
                max_x[i] = task_lanes[i].world_x + task_lanes[i].world_w;
                min_y[i] = task_lanes[i].world_y + task_lanes[i].world_h;
                max_y[i] = task_lanes[i].world_y; // 注意 Y 轴方向
            }

            Interval_Batch x_interval = { xs::load_aligned(min_x), xs::load_aligned(max_x) };
            Interval_Batch y_interval = { xs::load_aligned(min_y), xs::load_aligned(max_y) };

            // 区间求值 (Double)
            auto result = evaluate_rpn<Interval_Batch>(rpn_program_check, x_interval, y_interval);
            auto should_keep_mask = (result.min <= 0.0) & (result.max >= 0.0);

            if (xs::any(should_keep_mask)) {
                for (size_t i = 0; i < BATCH_SIZE; ++i) {
                    if (should_keep_mask.get(i)) {
                        const auto& task = task_lanes[i];
                        if (task.world_w < min_pixel_width || std::abs(task.world_h) < min_pixel_height) {
                            leaf_nodes.push_back(task); // 到达叶子节点
                        } else {
                            // 继续细分
                            double w_half = task.world_w / 2.0;
                            double h_half = task.world_h / 2.0;
                            tasks.push({task.world_x, task.world_y, w_half, h_half});
                            tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                            tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                            tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
                        }
                    }
                }
            }
        }

        // --- 标量尾部处理 ---
        while (!tasks.empty()) {
            QuadtreeTask task = tasks.top(); tasks.pop();
            Interval<double> x_interval(task.world_x, task.world_x + task.world_w);
            Interval<double> y_interval(task.world_y + task.world_h, task.world_y);
            auto result = evaluate_rpn<Interval<double>>(rpn_program_check, x_interval, y_interval);

            if (result.max >= 0.0 && result.min <= 0.0) {
                if (task.world_w < min_pixel_width || std::abs(task.world_h) < min_pixel_height) {
                    leaf_nodes.push_back(task);
                } else {
                    double w_half = task.world_w / 2.0;
                    double h_half = task.world_h / 2.0;
                    tasks.push({task.world_x, task.world_y, w_half, h_half});
                    tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                    tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                    tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
                }
            }
        }

        if (tasks.empty()) {
            break;
        }
    }

    // 3. 并行计算叶子节点 (Rasterization)
    oneapi::tbb::parallel_for_each(leaf_nodes.begin(), leaf_nodes.end(),
        [&](const QuadtreeTask& leaf) {
            ThreadCacheForTiling& cache = thread_local_caches.local();

            // 计算像素范围 (Screen Pixel Index)
            const double sx_start_f = (leaf.world_x - world_origin.x) / wppx;
            const double sy_start_f = (leaf.world_y - world_origin.y) / wppy;
            const double sx_end_f = (leaf.world_x + leaf.world_w - world_origin.x) / wppx;
            const double sy_end_f = (leaf.world_y + leaf.world_h - world_origin.y) / wppy;

            unsigned int x_start = static_cast<unsigned int>(std::floor(sx_start_f));
            unsigned int y_start = static_cast<unsigned int>(std::floor(sy_start_f));
            unsigned int x_end = static_cast<unsigned int>(std::ceil(sx_end_f));
            unsigned int y_end = static_cast<unsigned int>(std::ceil(sy_end_f));

            // 边界钳制
            x_start = std::max(0u, x_start);
            y_start = std::max(0u, y_start);
            x_end = std::min((unsigned int)screen_width, x_end);
            y_end = std::min((unsigned int)screen_height, y_end);

            if (x_start >= x_end || y_start >= y_end) return;

            // 执行核心绘制，传入 NDC Map
            draw_points_in_tile(
                concurrent_points,
                world_origin, wppx, wppy,
                rpn_program, rpn_program_check, func_idx,
                x_start, x_end, y_start, y_end,
                cache,
                ndc_map // ★★★ 传入映射
            );
        }
    );

    // 4. 导出结果
    std::vector<PointData> final_points(concurrent_points.begin(), concurrent_points.end());
    results_queue->push({func_idx, std::move(final_points)});
}