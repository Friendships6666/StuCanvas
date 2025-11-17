#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"

// 任务结构体，用于工业级高精度四叉树细分
struct IndustryQuadtreeTask {
    hp_float world_x, world_y;
    hp_float world_w, world_h;
};

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    // 1. 在函数开头创建一个常规的 vector 来收集所有点
    std::vector<PointData> local_points;
    local_points.reserve(20000); // 预估一个大小以减少重分配

    try {
        // 2. 解析RPN和精度
        IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);

        // 3. 为当前作用域的所有高精度浮点数设置默认精度
        hp_float::default_precision(rpn.precision_bits);

        // 4. 将所有需要的视图参数转换为高精度类型
        const hp_float hp_world_origin_x(world_origin.x);
        const hp_float hp_world_origin_y(world_origin.y);
        const hp_float hp_wppx(wppx);
        const hp_float hp_wppy(wppy);

        // 5. 初始化四叉树任务栈
        std::stack<IndustryQuadtreeTask> tasks;
        tasks.push({
            hp_float(world_origin.x), hp_float(world_origin.y),
            hp_float(screen_width * wppx), hp_float(screen_height * wppy)
        });

        // 6. 定义停止细分的条件
        const hp_float min_world_width = hp_float(0.5 * wppx);

        // 7. 开始四叉树细分循环
        while (!tasks.empty()) {
            IndustryQuadtreeTask task = tasks.top();
            tasks.pop();

            // 为当前任务区域创建高精度区间
            Interval<hp_float> x_interval(task.world_x, task.world_x + task.world_w);
            Interval<hp_float> y_interval(task.world_y + task.world_h, task.world_y); // Y轴是反的

            // 使用高精度区间算术评估RPN
            Interval<hp_float> result = evaluate_rpn<Interval<hp_float>>(
                rpn.program, x_interval, y_interval, std::nullopt, rpn.precision_bits
            );

            // 如果结果区间包含0，说明函数图像可能穿过此区域
            if (result.max >= 0.0 && result.min <= 0.0) {
                // 检查是否满足停止条件
                if (task.world_w < min_world_width) {
                    // 到达叶节点，计算世界坐标中心点
                    hp_float center_x_world = task.world_x + task.world_w / 2.0;
                    hp_float center_y_world = task.world_y + task.world_h / 2.0;

                    // 在高精度环境下进行 世界坐标 -> 屏幕坐标 的转换
                    hp_float screen_x = (center_x_world - hp_world_origin_x) / hp_wppx;
                    hp_float screen_y = (center_y_world - hp_world_origin_y) / hp_wppy;

                    // 将最终的屏幕坐标点存入局部向量
                    local_points.emplace_back(PointData{
                        { screen_x.convert_to<double>(), screen_y.convert_to<double>() },
                        func_idx
                    });

                } else {
                    // 未满足停止条件，继续细分为四个子任务
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

    // 8. 当函数完成所有计算后，将结果打包并推入队列
    results_queue->push({func_idx, std::move(local_points)});
}