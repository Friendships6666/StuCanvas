// --- 文件路径: src/plot/plotCall.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotIndustry.h"

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/global_control.h>
#include <map>

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list, // <--- 实现对应的新参数
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 清理输出容器
    out_points.clear();
    out_ranges.clear();

    const size_t total_implicit = implicit_rpn_pairs.size();
    const size_t total_industry = industry_rpn_list.size();
    const size_t total_functions = total_implicit + total_industry;

    if (total_functions == 0) return;

    // 2. 配置 TBB 并发控制
    const auto thread_count = std::thread::hardware_concurrency();
    // 限制最大并发数，防止在此处创建过多线程导致系统卡顿
    oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

    // 3. 准备结果队列和任务组
    // 使用 bounded_queue 可以防止生产者生产过快导致内存爆炸（但在这种场景下通常不需要限制）
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    // 设置一个较大的容量，避免不必要的阻塞
    results_queue.set_capacity(total_functions * 2);

    oneapi::tbb::task_group task_group;

    // 4. 计算世界坐标系参数
    // 屏幕中心 (0,0) 对应的世界坐标
    double aspect_ratio = screen_width / screen_height;
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);

    // 计算世界原点 (屏幕左上角对应的世界坐标，或者视图中心对应的世界坐标，取决于具体实现约定)
    // 这里的实现逻辑是：视图中心对应的 offset_x/y
    Vec2 world_origin = {
        (centered_x_0 / zoom) + offset_x,
        (centered_y_0 / zoom) + offset_y
    };

    // 每个像素代表的世界距离
    double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
    double world_per_pixel_y = -2.0 / (zoom * screen_height); // Y轴通常翻转

    // 5. 解析 RPN (为了避免在并行任务中重复解析，可以选择预先解析，或者在任务内解析)
    // 这里为了代码简洁和避免跨线程对象生命周期问题，我们在任务启动前解析隐函数
    std::vector<AlignedVector<RPNToken>> implicit_programs;
    std::vector<AlignedVector<RPNToken>> implicit_programs_check;
    implicit_programs.reserve(total_implicit);
    implicit_programs_check.reserve(total_implicit);

    for (const auto& pair : implicit_rpn_pairs) {
        implicit_programs.emplace_back(parse_rpn(pair.first));
        implicit_programs_check.emplace_back(parse_rpn(pair.second));
    }

    // 6. 派发任务：普通隐函数 (索引 0 ~ N-1)
    for (size_t i = 0; i < total_implicit; ++i) {
        // 按值捕获必要的参数
        task_group.run([&, i] {
            process_implicit_adaptive(
                &results_queue,
                world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                implicit_programs[i],
                implicit_programs_check[i],
                (unsigned int)i, // function_index
                offset_x, offset_y
            );
        });
    }

    for (size_t i = 0; i < total_industry; ++i) {
        task_group.run([&, i] {
            unsigned int func_idx = (unsigned int)(total_implicit + i);
            // 修正：移除最后一个参数 zoom，匹配新的函数签名
            process_single_industry_function(
                &results_queue,
                industry_rpn_list[i],
                func_idx,
                world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                offset_x, offset_y
            );
        });
    }

    // 8. 收集结果 (主线程作为消费者)
    // 我们知道总共有 total_functions 个任务会产生结果
    std::map<unsigned int, std::vector<PointData>> sorted_results;

    for (size_t i = 0; i < total_functions; ++i) {
        FunctionResult res;
        // 阻塞等待结果，直到取到一个
        results_queue.pop(res);

        // 简单的进度日志 (可选)
        // std::cout << "Received points for function " << res.function_index
        //           << ": " << res.points.size() << std::endl;

        // 将结果暂存到 map 中以按索引排序 (因为多线程完成顺序是不确定的)
        sorted_results[res.function_index] = std::move(res.points);
    }

    // 9. 等待所有任务彻底结束 (理论上上面循环结束时任务基本都完了，但这步确保安全)
    task_group.wait();

    // 10. 扁平化结果到 out_points 并构建 out_ranges
    out_ranges.resize(total_functions);

    // 预估总大小以减少 realloc
    size_t total_points_count = 0;
    for (const auto& kv : sorted_results) total_points_count += kv.second.size();
    out_points.reserve(total_points_count);

    uint32_t current_start_index = 0;

    for (unsigned int idx = 0; idx < total_functions; ++idx) {
        auto it = sorted_results.find(idx);
        if (it != sorted_results.end()) {
            const auto& points_vec = it->second;
            uint32_t count = (uint32_t)points_vec.size();

            // 记录 Range
            out_ranges[idx] = { current_start_index, count };

            // 拷贝点数据
            if (count > 0) {
                out_points.insert(out_points.end(), points_vec.begin(), points_vec.end());
                current_start_index += count;
            }
        } else {
            // 理论上不应发生，除非某个任务异常退出没推结果
            out_ranges[idx] = { current_start_index, 0 };
        }
    }
}