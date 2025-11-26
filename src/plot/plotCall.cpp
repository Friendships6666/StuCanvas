// --- 文件路径: src/plot/plotCall.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
// 移除 plotIndustry.h 的引用

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    // 移除 industry_rpn_list
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    out_points.clear();
    out_ranges.clear();

    const auto thread_count = std::thread::hardware_concurrency();
    oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    oneapi::tbb::task_group task_group;

    const size_t total_functions = implicit_rpn_pairs.size();
    if (total_functions == 0) return;

    // --- 准备计算参数 ---
    double aspect_ratio = screen_width / screen_height;
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);
    Vec2 world_origin = { (centered_x_0 / zoom) + offset_x, (centered_y_0 / zoom) + offset_y };
    double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
    double world_per_pixel_y = -2.0 / (zoom * screen_height);

    // --- 派发隐函数任务 ---
    std::vector<AlignedVector<RPNToken>> implicit_programs, implicit_programs_for_check;
    for(const auto& rpn_pair : implicit_rpn_pairs) {
        implicit_programs.push_back(parse_rpn(rpn_pair.first));
        implicit_programs_for_check.push_back(parse_rpn(rpn_pair.second));
    }
    for (size_t i = 0; i < implicit_programs.size(); ++i) {
        task_group.run([&, i] {
            process_implicit_adaptive(
                &results_queue, world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                implicit_programs[i], implicit_programs_for_check[i], (unsigned int)i,
                offset_x, offset_y
            );
        });
    }

    // --- 移除工业级函数派发逻辑 ---
    std::cout << "--- 开始实时接收和处理函数计算结果 ---" << std::endl;
    // --- 消费结果 ---
    std::map<unsigned int, std::vector<PointData>> collected_results;
    for (size_t i = 0; i < total_functions; ++i) {
        FunctionResult result;
        results_queue.pop(result);
        std::cout << "已完成函数 " << result.function_index
          << " 的计算，获得 " << result.points.size() << " 个点。" << std::endl;
        collected_results[result.function_index] = std::move(result.points);
    }
    std::cout << "--- 所有函数计算均已完成 ---" << std::endl;

    // --- 整理结果 ---
    std::vector<PointData> all_points_buffer;
    all_points_buffer.reserve(100000);
    out_ranges.resize(total_functions);
    uint32_t current_start_index = 0;

    for (unsigned int func_idx = 0; func_idx < total_functions; ++func_idx) {
        auto it = collected_results.find(func_idx);
        if (it != collected_results.end()) {
            const auto& points = it->second;
            uint32_t count = (uint32_t)points.size();
            out_ranges[func_idx] = {current_start_index, count};
            if (count > 0) {
                all_points_buffer.insert(all_points_buffer.end(), points.begin(), points.end());
            }
            current_start_index += count;
        } else {
            out_ranges[func_idx] = {current_start_index, 0};
        }
    }

    out_points.assign(all_points_buffer.begin(), all_points_buffer.end());
    task_group.wait();
}