#include "../../pch.h"
#include "../include/grids/grids.h"



double CalculateGridStep(double wpp) {
        double target_world_step = 90.0 * wpp;
        double exponent = std::floor(std::log10(target_world_step));
        double power_of_10 = std::pow(10.0, exponent);
        double fraction = target_world_step / power_of_10;

        if (fraction < 1.5)      return 1.0 * power_of_10;
        if (fraction < 3.5)      return 2.0 * power_of_10;
        if (fraction < 7.5)      return 5.0 * power_of_10;
        return 10.0 * power_of_10;
    }

    /**
     * @brief 极致性能：单次循环生成所有网格线，并区分 Major 和 Axis
     */
    void GenerateCartesianLines(
        std::vector<GridLineData>& buffer,
        std::vector<AxisIntersectionData>* intersection_buffer,
        const ViewState& v,
        uint64_t global_mask,
        double min_w, double max_w, double minor_step, double major_step,
        double ndc_scale, double offset,
        bool horizontal
    ) {
        // 预计算 16.16 增量
        int32_t step_fp = static_cast<int32_t>((minor_step * ndc_scale) * 65536.0);

        // 计算起始线的对齐世界坐标
        double first_w = std::floor(min_w / minor_step) * minor_step;
        int32_t cur_fp = static_cast<int32_t>(((first_w - offset) * ndc_scale) * 65536.0);
        double cur_w = first_w;

        // 终点限制
        int32_t end_fp = static_cast<int32_t>(32767) << 16;
        int32_t start_limit_fp = static_cast<int32_t>(-32767) << 16;

        // 判定阈值（使用 minor_step 的 10% 作为浮点数容差）
        double eps = minor_step * 0.1;

        while (cur_fp <= end_fp) {
            if (cur_fp >= start_limit_fp) {
                int16_t pos = static_cast<int16_t>(cur_fp >> 16);

                // 1. 判定是否为 Axis (坐标接近 0)
                bool is_axis = (std::abs(cur_w) < eps);

                // 2. 判定是否为 Major (坐标是 major_step 的倍数)
                double major_rem = std::abs(std::remainder(cur_w, major_step));
                bool is_major = (major_rem < eps);

                // 只有非轴线才放入网格 buffer（轴线在外部单独绘制以保证最高精度）
                if (!is_axis) {
                    if (horizontal)
                        buffer.push_back({ {-32767, pos}, {32767, pos}});
                    else
                        buffer.push_back({ {pos, -32767}, {pos, 32767}});

                    // 如果是 Major 线且有交点容器，记录交点信息 (受 DISABLE_GRID_NUMBER 控制)
                    if (is_major && intersection_buffer && !(global_mask & DISABLE_GRID_NUMBER)) {
                        Vec2i intersection_pos = horizontal ? v.WorldToClip(0, cur_w) : v.WorldToClip(cur_w, 0);
                        intersection_buffer->push_back({intersection_pos, cur_w});
                    }
                }
            }
            cur_fp += step_fp;
            cur_w += minor_step;
        }
    }