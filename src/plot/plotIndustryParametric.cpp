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

        if (parts.size() >= 5) {
            config.rpn_x_str = parts[0];
            config.rpn_y_str = parts[1];
            try {
                config.t_min = std::stod(parts[2]);
                config.t_max = std::stod(parts[3]);
                config.precision = std::stoul(parts[4]);
            } catch (...) {
                // Fallback defaults
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

            // 步长：0.1 像素
            int steps = static_cast<int>(dist * 10.0);
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
            MultiInterval<T> res_x = evaluate_rpn_multi<T>(m_prog_x, dummy_interval, dummy_interval, t_interval);
            MultiInterval<T> res_y = evaluate_rpn_multi<T>(m_prog_y, dummy_interval, dummy_interval, t_interval);

            // 3. 获取包围盒 (Hull)
            T box_min_x = get_inf_val<T>();
            T box_max_x = -get_inf_val<T>();
            T box_min_y = get_inf_val<T>();
            T box_max_y = -get_inf_val<T>();

            bool has_valid_part = false;

            for(int i=0; i<res_x.count; ++i) {
                if (res_x.parts[i].min < box_min_x) box_min_x = res_x.parts[i].min;
                if (res_x.parts[i].max > box_max_x) box_max_x = res_x.parts[i].max;
                has_valid_part = true;
            }
            // 如果完全无定义（例如 sqrt(-1)），则跳过
            if (!has_valid_part) return;

            has_valid_part = false;
            for(int i=0; i<res_y.count; ++i) {
                if (res_y.parts[i].min < box_min_y) box_min_y = res_y.parts[i].min;
                if (res_y.parts[i].max > box_max_y) box_max_y = res_y.parts[i].max;
                has_valid_part = true;
            }
            if (!has_valid_part) return;

            // 4. 视锥剔除 (Culling) - 使用安全边界
            // 如果包围盒完全在安全渲染区之外，则丢弃
            if (box_max_x < m_safe_min_x || box_min_x > m_safe_max_x ||
                box_max_y < m_safe_min_y || box_min_y > m_safe_max_y) {
                return;
            }

            // 5. 钳制包围盒 (Clamping)
            // 将无穷大或超大数值限制在安全渲染区内
            using std::max; using std::min;
            T clamp_min_x = max(box_min_x, m_safe_min_x);
            T clamp_max_x = min(box_max_x, m_safe_max_x);
            T clamp_min_y = max(box_min_y, m_safe_min_y);
            T clamp_max_y = min(box_max_y, m_safe_max_y);

            if (clamp_min_x > clamp_max_x || clamp_min_y > clamp_max_y) return;

            // 6. 终止判定
            using std::abs; using boost::multiprecision::abs;
            T pixel_w = abs((clamp_max_x - clamp_min_x) / m_wppx);
            T pixel_h = abs((clamp_max_y - clamp_min_y) / m_wppy);

            // 阈值：0.5 像素
            bool small_enough = (pixel_w < T(0.5)) && (pixel_h < T(0.5));

            // 如果足够小，或者达到了最大深度
            if (small_enough || depth >= MAX_DEPTH) {
                // ★★★ 核心修复：高精度坐标转换 ★★★
                // 先在 T 类型下减去 offset，再转 double，避免大坐标下的精度丢失
                double x1 = static_cast<double>(clamp_min_x - m_offset_x);
                double x2 = static_cast<double>(clamp_max_x - m_offset_x);
                double y1 = static_cast<double>(clamp_min_y - m_offset_y);
                double y2 = static_cast<double>(clamp_max_y - m_offset_y);

                // 描绘矩形边缘
                // 1. 下边 (Bottom)
                draw_line_segment(x1, y1, x2, y1, out_buffer);

                // 2. 右边 (Right)
                if (y2 > y1) draw_line_segment(x2, y1, x2, y2, out_buffer);

                // 3. 上边 (Top)
                if (y2 > y1) draw_line_segment(x2, y2, x1, y2, out_buffer);

                // 4. 左边 (Left)
                if (y2 > y1 && x2 > x1) draw_line_segment(x1, y2, x1, y1, out_buffer);

                return;
            }

            // 7. 递归细分
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

        static constexpr int MAX_DEPTH = 200;
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