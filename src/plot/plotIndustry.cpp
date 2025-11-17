#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"

namespace { // 使用匿名命名空间封装模板化实现细节

// 模板化的任务结构体，用于工业级高精度四叉树细分
template<typename T>
struct IndustryQuadtreeTaskT {
    T world_x, world_y;
    T world_w, world_h;
};

// 将模板类型T转换为double的辅助函数
template<typename T>
double convert_to_double(const T& val) {
    if constexpr (std::is_same_v<T, hp_float>) {
        return val.template convert_to<double>();
    } else {
        return val; // 如果T本身就是double，直接返回
    }
}

// 模板化的核心处理函数
template<typename T>
void execute_industry_processing(
    std::vector<PointData>& out_points, // 直接填充外部向量
    const IndustrialRPN& rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height
) {
    // 1. 将所有需要的视图参数转换为模板类型 T
    const T t_world_origin_x(world_origin.x);
    const T t_world_origin_y(world_origin.y);
    const T t_wppx(wppx);
    const T t_wppy(wppy);

    // 2. 初始化四叉树任务栈
    std::stack<IndustryQuadtreeTaskT<T>> tasks;
    tasks.push({
        T(world_origin.x), T(world_origin.y),
        T(screen_width * wppx), T(screen_height * wppy)
    });

    // 3. 定义停止细分的条件
    const T min_world_width = T(0.5 * wppx);

    // 4. 开始四叉树细分循环
    while (!tasks.empty()) {
        IndustryQuadtreeTaskT<T> task = tasks.top();
        tasks.pop();

        // 为当前任务区域创建高精度区间
        Interval<T> x_interval(task.world_x, task.world_x + task.world_w);
        Interval<T> y_interval(task.world_y + task.world_h, task.world_y); // Y轴是反的

        // 使用高精度区间算术评估RPN
        Interval<T> result = evaluate_rpn<Interval<T>>(
            rpn.program, x_interval, y_interval, std::nullopt, rpn.precision_bits
        );

        // 如果结果区间包含0，说明函数图像可能穿过此区域
        if (result.max >= T(0.0) && result.min <= T(0.0)) {
            // 检查是否满足停止条件
            if (task.world_w < min_world_width) {
                // 到达叶节点，计算世界坐标中心点
                T center_x_world = task.world_x + task.world_w / T(2.0);
                T center_y_world = task.world_y + task.world_h / T(2.0);

                // 在高精度环境下进行 世界坐标 -> 屏幕坐标 的转换
                T screen_x = (center_x_world - t_world_origin_x) / t_wppx;
                T screen_y = (center_y_world - t_world_origin_y) / t_wppy;

                // 将最终的屏幕坐标点存入局部向量
                out_points.emplace_back(PointData{
                    { convert_to_double(screen_x), convert_to_double(screen_y) },
                    func_idx
                });

            } else {
                // 未满足停止条件，继续细分为四个子任务
                T w_half = task.world_w / T(2.0);
                T h_half = task.world_h / T(2.0);
                tasks.push({task.world_x, task.world_y, w_half, h_half});
                tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
            }
        }
    }
}

} // 匿名命名空间结束

// 公共接口函数，现在作为分发器
void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    std::vector<PointData> local_points;
    local_points.reserve(20000); // 预估大小以减少内存重分配

    try {
        // 解析RPN和精度
        IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);

        // 根据精度值选择不同的计算路径
        if (rpn.precision_bits == 0) {
            // 精度为0，启动双精度(double)计算模式
            execute_industry_processing<double>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        } else {
            // 精度大于0，启动任意精度(hp_float)计算模式
            hp_float::default_precision(rpn.precision_bits);
            execute_industry_processing<hp_float>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        }
    } catch (const std::exception& e) {
        std::cerr << "处理工业级RPN时出错 '" << industry_rpn << "': " << e.what() << std::endl;
    }

    // 当函数完成所有计算后，将结果打包并推入队列
    results_queue->push({func_idx, std::move(local_points)});
}