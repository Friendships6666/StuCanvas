#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"

struct IndustryQuadtreeTask {
    hp_float world_x, world_y;
    hp_float world_w, world_h;
};

void process_single_industry_function(
    oneapi::tbb::concurrent_vector<PointData>& out_points,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    try {
        IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);
        hp_float::default_precision(rpn.precision_bits);

        // 将视图参数转换为高精度类型，以备后续计算
        const hp_float hp_world_origin_x(world_origin.x);
        const hp_float hp_world_origin_y(world_origin.y);
        const hp_float hp_wppx(wppx);
        const hp_float hp_wppy(wppy);

        std::stack<IndustryQuadtreeTask> tasks;
        tasks.push({
            hp_float(world_origin.x), hp_float(world_origin.y),
            hp_float(screen_width * wppx), hp_float(screen_height * wppy)
        });

        const hp_float min_world_width = hp_float(0.5* wppx);

        while (!tasks.empty()) {
            IndustryQuadtreeTask task = tasks.top();
            tasks.pop();

            Interval<hp_float> x_interval(task.world_x, task.world_x + task.world_w);
            Interval<hp_float> y_interval(task.world_y + task.world_h, task.world_y);

            Interval<hp_float> result = evaluate_rpn<Interval<hp_float>>(
                rpn.program, x_interval, y_interval, std::nullopt, rpn.precision_bits
            );

            if (result.max >= 0.0 && result.min <= 0.0) {
                if (task.world_w < min_world_width) {
                    hp_float center_x_world = task.world_x + task.world_w / 2.0;
                    hp_float center_y_world = task.world_y + task.world_h / 2.0;

                    // ====================================================================
                    //          ↓↓↓ 在高精度环境下进行 世界坐标 -> 屏幕坐标 的转换 ↓↓↓
                    // ====================================================================
                    hp_float screen_x = (center_x_world - hp_world_origin_x) / hp_wppx;
                    hp_float screen_y = (center_y_world - hp_world_origin_y) / hp_wppy;

                    // 将最终的屏幕坐标转换为double并存入结果
                    out_points.emplace_back(PointData{
                        { screen_x.convert_to<double>(), screen_y.convert_to<double>() },
                        func_idx
                    });

                } else {
                    hp_float w_half = task.world_w / 2.0;
                    hp_float h_half = task.world_h / 2.0;
                    tasks.push({task.world_x, task.world_y, w_half, h_half});
                    tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                    tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                    tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "处理工业级RPN时出错 '" << industry_rpn << "': " << e.what() << std::endl;
    }
}