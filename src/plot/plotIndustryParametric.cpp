// --- 文件路径: src/plot/plotIndustryParametric.cpp ---

#include "../../include/plot/plotIndustryParametric.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/CAS/RPN/MultiIntervalRPN.h"
#include "../../include/interval/MultiInterval.h"
#include "../../include/functions/lerp.h"

#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace {

    // =========================================================
    // 配置解析结构体
    // =========================================================
    struct ParametricConfig {
        std::string rpn_x_str;
        std::string rpn_y_str;
        double t_min = 0.0;
        double t_max = 1.0;
        unsigned int precision = 0;
    };

    ParametricConfig parse_parametric_config(const std::string& input) {
        ParametricConfig config;
        std::stringstream ss(input);
        std::string segment;
        std::vector<std::string> parts;

        while (std::getline(ss, segment, ';')) {
            parts.push_back(segment);
        }

        if (parts.size() >= 3) {
            config.rpn_x_str = parts[0];
            config.rpn_y_str = parts[1];
            try {
                // 必须至少包含 x_rpn, y_rpn, t_min, t_max
                // 如果没有 precision，默认为 0
                if (parts.size() >= 4) {
                    config.t_min = std::stod(parts[2]);
                    config.t_max = std::stod(parts[3]);
                }
                if (parts.size() >= 5) {
                    config.precision = std::stoul(parts[4]);
                }
            } catch (...) {
                // 解析失败使用默认值
            }
        }
        return config;
    }

    // =========================================================
    // 核心求解器模板 (支持 double 和 hp_float)
    // =========================================================
    template<typename T>
    class ParametricSolver {
    public:
        ParametricSolver(
            const AlignedVector<RPNToken>& prog_x,
            const AlignedVector<RPNToken>& prog_y,
            const Vec2& origin,
            double wppx, double wppy,
            double sw, double sh,
            double off_x, double off_y,
            unsigned int func_id
        ) : m_prog_x(prog_x), m_prog_y(prog_y),
            m_origin_x(T(origin.x)), m_origin_y(T(origin.y)),
            m_wppx(T(wppx)), m_wppy(T(wppy)),
            m_screen_w(T(sw)), m_screen_h(T(sh)),
            m_offset_x(T(off_x)), m_offset_y(T(off_y)), // 注意：这里将 offset 转为 T 存储
            m_func_idx(func_id)
        {
            // 计算屏幕的世界坐标边界
            T wx1 = m_origin_x;
            T wx2 = m_origin_x + m_screen_w * m_wppx;
            T wy1 = m_origin_y;
            T wy2 = m_origin_y + m_screen_h * m_wppy;

            using std::min; using std::max;
            m_world_min_x = min(wx1, wx2);
            m_world_max_x = max(wx1, wx2);
            m_world_min_y = min(wy1, wy2);
            m_world_max_y = max(wy1, wy2);

            // 安全渲染框：扩大 10% 以防止边缘裁剪，同时作为无穷大的钳制边界
            T margin_x = (m_world_max_x - m_world_min_x) * T(0.1);
            T margin_y = (m_world_max_y - m_world_min_y) * T(0.1);

            m_safe_min_x = m_world_min_x - margin_x;
            m_safe_max_x = m_world_max_x + margin_x;
            m_safe_min_y = m_world_min_y - margin_y;
            m_safe_max_y = m_world_max_y + margin_y;
        }

        // 辅助：在两点之间生成密集点 (描边)
        // 输入的坐标已经是相对于 offset 的“屏幕空间世界坐标”
        void draw_line_segment(double x1, double y1, double x2, double y2, std::vector<PointData>& out_buffer) const {
            // 转换为像素距离
            double dx_pix = (x2 - x1) / static_cast<double>(m_wppx);
            double dy_pix = (y2 - y1) / static_cast<double>(m_wppy);
            double dist = std::sqrt(dx_pix * dx_pix + dy_pix * dy_pix);

            // 步长：0.5 像素 (稍微密集一点以保证连贯性)
            int steps = static_cast<int>(dist * 2.0);
            if (steps == 0) steps = 1;

            // 限制单条边的最大点数，防止极端情况卡死
            if (steps > 2000) steps = 2000;

            for (int i = 0; i <= steps; ++i) {
                double t = static_cast<double>(i) / steps;
                PointData pd;
                // 这里的 x1, y1 已经减去了 offset，所以直接赋值
                pd.position.x = x1 + t * (x2 - x1);
                pd.position.y = y1 + t * (y2 - y1);
                pd.function_index = m_func_idx;
                out_buffer.push_back(pd);
            }
        }

        // 递归细分函数
        void solve_recursive(T t_start, T t_end, int depth, std::vector<PointData>& out_buffer) const {
            // 1. 构造 t 的区间
            Interval<T> t_interval(t_start, t_end);
            Interval<T> dummy_interval(T(0));

            // 2. 区间评估 (Interval Arithmetic)
            // 使用 MultiInterval 处理断裂 (例如 1/t 在 t=0 处)
            MultiInterval<T> res_x = evaluate_rpn_multi<T>(m_prog_x, dummy_interval, dummy_interval, t_interval);
            MultiInterval<T> res_y = evaluate_rpn_multi<T>(m_prog_y, dummy_interval, dummy_interval, t_interval);

            // 3. 计算合并的包围盒 (Merged Hull) 用于决策
            //    虽然我们最终会分开绘制，但为了判断是否需要继续细分，我们需要一个整体范围
            T merged_min_x = get_inf_val<T>();
            T merged_max_x = -get_inf_val<T>();
            T merged_min_y = get_inf_val<T>();
            T merged_max_y = -get_inf_val<T>();

            bool has_valid_part = false;

            for(int i=0; i<res_x.count; ++i) {
                if (res_x.parts[i].min < merged_min_x) merged_min_x = res_x.parts[i].min;
                if (res_x.parts[i].max > merged_max_x) merged_max_x = res_x.parts[i].max;
                has_valid_part = true;
            }
            if (!has_valid_part) return; // X 无定义

            has_valid_part = false;
            for(int i=0; i<res_y.count; ++i) {
                if (res_y.parts[i].min < merged_min_y) merged_min_y = res_y.parts[i].min;
                if (res_y.parts[i].max > merged_max_y) merged_max_y = res_y.parts[i].max;
                has_valid_part = true;
            }
            if (!has_valid_part) return; // Y 无定义

            // 4. 视锥剔除 (Culling) - 基于合并包围盒
            // 如果连最大的范围都在屏幕外，那就直接放弃
            if (merged_max_x < m_safe_min_x || merged_min_x > m_safe_max_x ||
                merged_max_y < m_safe_min_y || merged_min_y > m_safe_max_y) {
                return;
            }

            // 5. 计算屏幕上的像素尺寸 (用于终止判定)
            //    注意：我们 clamp 到安全边界，避免无穷大导致计算溢出
            using std::max; using std::min; using std::abs; using boost::multiprecision::abs;

            T clamp_min_x = max(merged_min_x, m_safe_min_x);
            T clamp_max_x = min(merged_max_x, m_safe_max_x);
            T clamp_min_y = max(merged_min_y, m_safe_min_y);
            T clamp_max_y = min(merged_max_y, m_safe_max_y);

            T pixel_w = abs((clamp_max_x - clamp_min_x) / m_wppx);
            T pixel_h = abs((clamp_max_y - clamp_min_y) / m_wppy);

            // 阈值：0.5 像素
            bool small_enough = (pixel_w < T(0.5)) && (pixel_h < T(0.5));

            // 6. 终止判定与绘制
            if (small_enough || depth >= MAX_DEPTH) {
                // ★★★ 核心修复 ★★★
                // 不绘制合并后的包围盒，而是遍历所有组合 (Cartesian Product) 分别绘制。
                // 这样，如果有断裂 (例如中间空了一大块)，就不会被错误的连线填满。

                for(int i=0; i<res_x.count; ++i) {
                    for(int j=0; j<res_y.count; ++j) {
                        // 获取当前组合的局部边界
                        T lx1 = res_x.parts[i].min;
                        T lx2 = res_x.parts[i].max;
                        T ly1 = res_y.parts[j].min;
                        T ly2 = res_y.parts[j].max;

                        // 局部剔除：检查这个小块是否在屏幕外
                        if (lx2 < m_safe_min_x || lx1 > m_safe_max_x ||
                            ly2 < m_safe_min_y || ly1 > m_safe_max_y) continue;

                        // 局部钳制：将无穷大限制在安全边界内
                        T cx1 = max(lx1, m_safe_min_x);
                        T cx2 = min(lx2, m_safe_max_x);
                        T cy1 = max(ly1, m_safe_min_y);
                        T cy2 = min(ly2, m_safe_max_y);

                        // 坐标转换：减去 offset 并转为 double
                        // 在高精度模式下，先减 offset 再转 double 至关重要
                        double d_x1 = static_cast<double>(cx1 - m_offset_x);
                        double d_x2 = static_cast<double>(cx2 - m_offset_x);
                        double d_y1 = static_cast<double>(cy1 - m_offset_y);
                        double d_y2 = static_cast<double>(cy2 - m_offset_y);

                        // 绘制矩形边框 (或者你可以只画对角线，取决于视觉风格)
                        // 这里我们画矩形轮廓以确保覆盖所有像素
                        draw_line_segment(d_x1, d_y1, d_x2, d_y1, out_buffer); // Bottom
                        if (d_y2 > d_y1) draw_line_segment(d_x2, d_y1, d_x2, d_y2, out_buffer); // Right
                        if (d_y2 > d_y1) draw_line_segment(d_x2, d_y2, d_x1, d_y2, out_buffer); // Top
                        if (d_y2 > d_y1 && d_x2 > d_x1) draw_line_segment(d_x1, d_y2, d_x1, d_y1, out_buffer); // Left
                    }
                }
                return;
            }

            // 7. 递归细分
            // 如果包围盒还很大，说明需要更精细的 t
            T t_mid = t_start + (t_end - t_start) * T(0.5);
            solve_recursive(t_start, t_mid, depth + 1, out_buffer);
            solve_recursive(t_mid, t_end, depth + 1, out_buffer);
        }

    private:
        const AlignedVector<RPNToken>& m_prog_x;
        const AlignedVector<RPNToken>& m_prog_y;
        T m_origin_x, m_origin_y;
        T m_wppx, m_wppy;
        T m_screen_w, m_screen_h;
        T m_offset_x, m_offset_y; // 使用 T 类型存储 offset
        unsigned int m_func_idx;

        T m_world_min_x, m_world_max_x;
        T m_world_min_y, m_world_max_y;
        T m_safe_min_x, m_safe_max_x;
        T m_safe_min_y, m_safe_max_y;

        // 最大递归深度，防止栈溢出
        static constexpr int MAX_DEPTH = 20;
    };

} // namespace anonymous


void process_industry_parametric(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& param_config_str,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y
) {
    // 1. 解析配置
    ParametricConfig config = parse_parametric_config(param_config_str);

    // 2. 解析 RPN
    AlignedVector<RPNToken> prog_x = parse_rpn(config.rpn_x_str);
    AlignedVector<RPNToken> prog_y = parse_rpn(config.rpn_y_str);

    // 3. 准备并行分块
    constexpr int NUM_BLOCKS = 256;
    std::vector<std::vector<PointData>> block_results(NUM_BLOCKS);

    // 4. 执行计算
    if (config.precision == 0) {
        using T = double;
        ParametricSolver<T> solver(
            prog_x, prog_y, world_origin, wppx, wppy,
            screen_width, screen_height, offset_x, offset_y, func_idx
        );

        T t_range = config.t_max - config.t_min;
        T t_step = t_range / T(NUM_BLOCKS);

        oneapi::tbb::parallel_for(0, NUM_BLOCKS, [&](int i) {
            T t_start = config.t_min + T(i) * t_step;
            T t_end = config.t_min + T(i + 1) * t_step;

            block_results[i].reserve(1024);
            solver.solve_recursive(t_start, t_end, 0, block_results[i]);
        });

    } else {
        using T = hp_float;
        hp_float::default_precision(config.precision);

        ParametricSolver<T> solver(
            prog_x, prog_y, world_origin, wppx, wppy,
            screen_width, screen_height, offset_x, offset_y, func_idx
        );

        T t_min_hp = T(config.t_min);
        T t_max_hp = T(config.t_max);
        T t_range = t_max_hp - t_min_hp;
        T t_step = t_range / T(NUM_BLOCKS);

        oneapi::tbb::parallel_for(0, NUM_BLOCKS, [&](int i) {
            // 在每个线程中设置精度（尽管 TBB 通常复用线程，但为了保险起见）
            hp_float::default_precision(config.precision);

            T t_start = t_min_hp + T(i) * t_step;
            T t_end = t_min_hp + T(i + 1) * t_step;

            block_results[i].reserve(1024);
            solver.solve_recursive(t_start, t_end, 0, block_results[i]);
        });
    }

    // 5. 合并结果
    std::vector<PointData> final_points;
    size_t total_points = 0;
    for(const auto& vec : block_results) total_points += vec.size();
    final_points.reserve(total_points);

    for(auto& vec : block_results) {
        final_points.insert(final_points.end(), vec.begin(), vec.end());
    }

    // 6. 推送结果
    results_queue->push({func_idx, std::move(final_points)});
}