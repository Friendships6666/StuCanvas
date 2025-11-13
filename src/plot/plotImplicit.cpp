// --- 文件路径: src/plot/plotImplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/functions/lerp.h"
#include "../../include/functions/functions.h"

// 任务结构体，用于四叉树细分
struct QuadtreeTask {
    double world_x, world_y; // 区块左上角的世界坐标
    double world_w, world_h; // 区块在世界坐标系下的宽高
};

// ThreadCacheForTiling 构造函数保持不变
ThreadCacheForTiling::ThreadCacheForTiling() {
    top_row_vals.resize(TILE_W + 1);
    bot_row_vals.resize(TILE_W + 1);
    point_buffer.reserve(3000);
}

// 阶段 2: 精绘函数 (保持不变)
void draw_points_in_tile(const Vec2& world_origin, double wppx, double wppy,
                         const AlignedVector<RPNToken>& rpn_program, const AlignedVector<RPNToken>& rpn_program_check,
                         unsigned int func_idx, unsigned int x_start, unsigned int x_end, unsigned int y_start, unsigned int y_end,
                         ThreadCacheForTiling& cache, oneapi::tbb::concurrent_vector<PointData>& all_points) {
    const unsigned int tile_w = x_end - x_start;
    // 增加一个安全检查，防止tile_w为0导致后续访问越界
    if (tile_w > TILE_W || tile_w == 0) return;

    auto& top_row_vals = cache.top_row_vals;
    auto& bot_row_vals = cache.bot_row_vals;
    auto& point_buffer = cache.point_buffer;

    for (unsigned int x = x_start; x <= x_end; ++x) {
        top_row_vals[x - x_start] = evaluate_rpn<double>(rpn_program, world_origin.x + x * wppx, world_origin.y + y_start * wppy);
    }

    for (unsigned int y = y_start; y < y_end; ++y) {
        const std::size_t vec_end = tile_w - (tile_w % batch_type::size);
        for (std::size_t x_off = 0; x_off < vec_end; x_off += batch_type::size) {
            batch_type sx = get_index_vec() + static_cast<double>(x_start + x_off);
            auto [wx, wy] = screen_to_world_batch(sx, (double)y + 1.0, world_origin, wppx, wppy);
            xs::store_aligned(&bot_row_vals[x_off], evaluate_rpn<batch_type>(rpn_program, wx, wy));
        }
        for (std::size_t x_off = vec_end; x_off <= tile_w; ++x_off) {
            Vec2 world_pos = screen_to_world_inline({(double)(x_start + x_off), (double)y + 1.0}, world_origin, wppx, wppy);
            bot_row_vals[x_off] = evaluate_rpn<double>(rpn_program, world_pos.x, world_pos.y);
        }

        point_buffer.clear();
        for (unsigned int x_off = 0; x_off < tile_w; ++x_off) {
            double tl = top_row_vals[x_off], tr = top_row_vals[x_off + 1], bl = bot_row_vals[x_off];
            if (!std::isfinite(tl) || !std::isfinite(tr) || !std::isfinite(bl)) continue;

            double sign_tl = (tl > 0.0) - (tl < 0.0);
            if (((tr > 0.0) - (tr < 0.0)) == sign_tl && ((bl > 0.0) - (bl < 0.0)) == sign_tl) continue;

            constexpr double step = 0.5;
            Vec2 intersection;
            for (int ly = 0; ly < 2; ++ly) for (int lx = 0; lx < 2; ++lx) {
                Vec2 s_tl_scr = {(double)(x_start + x_off) + lx * step, (double)y + ly * step};
                Vec2 p_tl = screen_to_world_inline(s_tl_scr, world_origin, wppx, wppy);
                Vec2 p_tr = screen_to_world_inline({s_tl_scr.x + step, s_tl_scr.y}, world_origin, wppx, wppy);
                Vec2 p_bl = screen_to_world_inline({s_tl_scr.x, s_tl_scr.y + step}, world_origin, wppx, wppy);
                double v_tl = evaluate_rpn<double>(rpn_program, p_tl.x, p_tl.y);
                double v_tr = evaluate_rpn<double>(rpn_program, p_tr.x, p_tr.y);
                double v_bl = evaluate_rpn<double>(rpn_program, p_bl.x, p_bl.y);

                if (!std::isfinite(v_tl) || !std::isfinite(v_tr) || !std::isfinite(v_bl)) continue;

                if (try_get_intersection_point(intersection, p_tl, p_tr, v_tl, v_tr, rpn_program_check))
                    point_buffer.emplace_back(PointData{intersection, func_idx});
                if (try_get_intersection_point(intersection, p_tl, p_bl, v_tl, v_bl, rpn_program_check))
                    point_buffer.emplace_back(PointData{intersection, func_idx});
            }
        }
        if (!point_buffer.empty()) {
            all_points.grow_by(point_buffer.begin(), point_buffer.end());
        }
        std::swap(top_row_vals, bot_row_vals);
    }
}


/**
 * @brief 隐函数绘图的混合实现方案
 *
 * 该函数采用两阶段策略，以实现最佳性能：
 * 1. 阶段一：自适应四叉树粗粒度剔除。
 *    - 从整个屏幕开始，使用SIMD加速的区间算术递归地细分空间。
 *    - 快速地丢弃不包含函数解的大片空白区域。
 *    - 当一个区域小到一定阈值（例如256x256像素）且仍然可能包含解时，停止细分，
 *      并将这个“活跃区块”添加到一个列表中。
 * 2. 阶段二：并行精细绘制。
 *    - 使用 TBB 的 `parallel_for_each` 对所有收集到的“活跃区块”列表进行并行处理。
 *    - 这种方法的任务粒度非常理想：每个任务都是一个已知包含工作量的、大小适中的区块。
 *    - 在每个并行任务中，调用 `draw_points_in_tile` 函数进行高精度的点生成。
 *
 * 这种混合策略兼具四叉树的剔除效率和并行网格处理的高吞吐量。
 */
void process_implicit_adaptive(
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    oneapi::tbb::combinable<ThreadCacheForTiling>& thread_local_caches,
    oneapi::tbb::concurrent_vector<PointData>& all_points
) {
    std::stack<QuadtreeTask> tasks;
    std::vector<QuadtreeTask> leaf_nodes; // 存储需要精绘的“活跃区块”

    tasks.push({
        world_origin.x, world_origin.y,
        screen_width * wppx, screen_height * wppy
    });

    // ====================================================================
    //  MODIFIED: 定义一个更合理的递归终止尺寸
    //  - 不再是固定的200像素，而是相当于256x256像素的区块。
    //  - 这为并行化提供了更大、更有效的任务单元。
    // ====================================================================
    const double MIN_ADAPTIVE_WORLD_WIDTH = 256.0 * wppx;
    const double MIN_ADAPTIVE_WORLD_HEIGHT = 256.0 * std::abs(wppy);

    alignas(batch_type::arch_type::alignment()) double min_x[BATCH_SIZE], max_x[BATCH_SIZE];
    alignas(batch_type::arch_type::alignment()) double min_y[BATCH_SIZE], max_y[BATCH_SIZE];
    QuadtreeTask task_lanes[BATCH_SIZE];

    // --- 阶段一: 四叉树粗粒度剔除 ---
    while (true) {
        // --- 快车道: SIMD 主批处理循环 ---
        while (tasks.size() >= BATCH_SIZE) {
            for (size_t i = 0; i < BATCH_SIZE; ++i) {
                task_lanes[i] = tasks.top(); tasks.pop();
                min_x[i] = task_lanes[i].world_x;
                max_x[i] = task_lanes[i].world_x + task_lanes[i].world_w;
                min_y[i] = task_lanes[i].world_y + task_lanes[i].world_h; // wppy < 0
                max_y[i] = task_lanes[i].world_y;
            }

            Interval_Batch x_interval = { xs::load_aligned(min_x), xs::load_aligned(max_x) };
            Interval_Batch y_interval = { xs::load_aligned(min_y), xs::load_aligned(max_y) };
            Interval_Batch result = evaluate_rpn<Interval_Batch>(rpn_program, x_interval, y_interval);

            auto should_keep_mask = (result.min <= 0.0) & (result.max >= 0.0);

            if (xs::any(should_keep_mask)) {
                for (size_t i = 0; i < BATCH_SIZE; ++i) {
                    if (should_keep_mask.get(i)) {
                        const auto& task = task_lanes[i];
                        if (task.world_w < MIN_ADAPTIVE_WORLD_WIDTH || std::abs(task.world_h) < MIN_ADAPTIVE_WORLD_HEIGHT) {
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
            }
        }

        // --- 慢车道: 标量清扫循环 ---
        while (!tasks.empty()) {
            QuadtreeTask task = tasks.top(); tasks.pop();

            Interval x_interval(task.world_x, task.world_x + task.world_w);
            Interval y_interval(task.world_y + task.world_h, task.world_y);
            Interval result = evaluate_rpn<Interval>(rpn_program, x_interval, y_interval);

            if (result.max >= 0.0 && result.min <= 0.0) {
                if (task.world_w < MIN_ADAPTIVE_WORLD_WIDTH || std::abs(task.world_h) < MIN_ADAPTIVE_WORLD_HEIGHT) {
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

    // --- 阶段二: 并行精细绘制 ---
    // 对所有筛选出的活跃区块，进行大规模并行处理
    oneapi::tbb::parallel_for_each(leaf_nodes.begin(), leaf_nodes.end(),
        [&](const QuadtreeTask& leaf) {
            ThreadCacheForTiling& cache = thread_local_caches.local();

            // 将世界坐标区块转换回像素坐标
            unsigned int x_start = static_cast<unsigned int>((leaf.world_x - world_origin.x) / wppx);
            unsigned int y_start = static_cast<unsigned int>((leaf.world_y - world_origin.y) / wppy);
            unsigned int x_end = static_cast<unsigned int>((leaf.world_x + leaf.world_w - world_origin.x) / wppx);
            unsigned int y_end = static_cast<unsigned int>((leaf.world_y + leaf.world_h - world_origin.y) / wppy);

            // 边界裁剪
            x_start = std::max(0u, x_start);
            y_start = std::max(0u, y_start);
            x_end = std::min((unsigned int)screen_width, x_end);
            y_end = std::min((unsigned int)screen_height, y_end);

            if (x_start >= x_end || y_start >= y_end) return;

            // 在这个活跃区块内进行高精度绘制
            draw_points_in_tile(
                world_origin, wppx, wppy,
                rpn_program, rpn_program_check, func_idx,
                x_start, x_end, y_start, y_end,
                cache, all_points
            );
        }
    );
}