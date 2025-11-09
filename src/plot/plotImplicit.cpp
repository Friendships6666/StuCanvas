// --- 文件路径: src/plot/plotImplicit.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/functions/lerp.h"
#include "../../include/functions/functions.h"
#include <stack> // 用于深度优先的四叉树遍历
#include <oneapi/tbb/parallel_for_each.h>
#include <iostream> // 用于调试输出
#include <iomanip>  // 用于格式化输出

// 任务结构体，用于四叉树细分
struct QuadtreeTask {
    double world_x, world_y; // 区块左上角的世界坐标
    double world_w, world_h; // 区块在世界坐标系下的宽高
};

// ----------------------------------------------------------------------------------
//  调试辅助：在文件作用域内添加一个 Interval 的打印函数
// ----------------------------------------------------------------------------------
namespace {
    std::ostream& operator<<(std::ostream& os, const Interval& i) {
        os << "[" << i.min << ", " << i.max << "]";
        return os;
    }
}
// ----------------------------------------------------------------------------------


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
            for (const auto& p : point_buffer)
                all_points.emplace_back(p);
        }
        std::swap(top_row_vals, bot_row_vals);
    }
}


// 阶段 1: 粗筛主函数 (已修正并添加调试信息)
void process_implicit_adaptive(
    const Vec2& world_origin, double wppx, double wppy,
    double screen_width, double screen_height,
    const AlignedVector<RPNToken>& rpn_program,
    const AlignedVector<RPNToken>& rpn_program_check,
    unsigned int func_idx,
    oneapi::tbb::combinable<ThreadCacheForTiling>& thread_local_caches,
    oneapi::tbb::concurrent_vector<PointData>& all_points
) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "--- [函数 " << func_idx << "] 开始自适应隐函数处理 ---\n";

    std::stack<QuadtreeTask> tasks;
    std::vector<QuadtreeTask> leaf_nodes;

    tasks.push({
        world_origin.x, world_origin.y,
        screen_width * wppx, screen_height * wppy
    });

    const double min_pixel_width = 10 * wppx;
    const double min_pixel_height = 10 * std::abs(wppy);
    int iteration_count = 0;

    while (!tasks.empty()) {
        iteration_count++;
        QuadtreeTask task = tasks.top();
        tasks.pop();

        // --- 调试信息: 打印当前处理的区块 ---
        // std::cout << "迭代 " << iteration_count << ": 处理区块 X=[" << task.world_x << ", " << task.world_x + task.world_w
        //           << "], Y=[" << task.world_y + task.world_h << ", " << task.world_y << "]\n";

        // 1. 区间算术剔除
        Interval x_interval(task.world_x, task.world_x + task.world_w);

        // ====================================================================
        //  BUG 修正点
        //  因为 world_h 是负数, 所以 y 区间的 min 是 world_y + world_h
        // ====================================================================
        Interval y_interval(task.world_y + task.world_h, task.world_y);

        Interval result = evaluate_rpn<Interval>(rpn_program, x_interval, y_interval);

        // --- 调试信息: 打印区间计算结果 ---
        // std::cout << "    X 区间: " << x_interval << ", Y 区间: " << y_interval << " -> 结果区间: " << result << "\n";

        if (result.max < 0.0 || result.min > 0.0) {
            // --- 调试信息: 打印被剔除的区块 ---
            // std::cout << "    -> 结果不包含0, 剔除该区块。\n";
            continue;
        }

        if (task.world_w < min_pixel_width || std::abs(task.world_h) < min_pixel_height) {
            // --- 调试信息: 打印找到的叶子节点 ---
            // std::cout << "    -> 区块足够小, 添加为叶子节点。\n";
            leaf_nodes.push_back(task);
            continue;
        }

        double w_half = task.world_w / 2.0;
        double h_half = task.world_h / 2.0;
        double x_mid = task.world_x + w_half;
        double y_mid = task.world_y + h_half;

        tasks.push({task.world_x, task.world_y, w_half, h_half}); // 左上
        tasks.push({x_mid, task.world_y, w_half, h_half});        // 右上
        tasks.push({task.world_x, y_mid, w_half, h_half});        // 左下
        tasks.push({x_mid, y_mid, w_half, h_half});               // 右下
    }

    // --- 调试信息: 打印最终结果 ---
    std::cout << "--- [函数 " << func_idx << "] 粗筛完成。总共找到 " << leaf_nodes.size() << " 个需要精绘的活跃区块。\n";

    oneapi::tbb::parallel_for_each(leaf_nodes.begin(), leaf_nodes.end(),
        [&](const QuadtreeTask& leaf) {
            ThreadCacheForTiling& cache = thread_local_caches.local();

            unsigned int x_start = static_cast<unsigned int>((leaf.world_x - world_origin.x) / wppx);
            unsigned int y_start = static_cast<unsigned int>((leaf.world_y - world_origin.y) / wppy);
            unsigned int x_end = static_cast<unsigned int>((leaf.world_x + leaf.world_w - world_origin.x) / wppx);
            unsigned int y_end = static_cast<unsigned int>((leaf.world_y + leaf.world_h - world_origin.y) / wppy);

            x_start = std::max(0u, x_start);
            y_start = std::max(0u, y_start);
            x_end = std::min((unsigned int)screen_width, x_end);
            y_end = std::min((unsigned int)screen_height, y_end);

            if (x_start >= x_end || y_start >= y_end) return;

            draw_points_in_tile(
                world_origin, wppx, wppy,
                rpn_program, rpn_program_check, func_idx,
                x_start, x_end, y_start, y_end,
                cache, all_points
            );
        }
    );
}