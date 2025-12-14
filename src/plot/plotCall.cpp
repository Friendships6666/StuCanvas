// --- 文件路径: src/plot/plotCall.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotIndustry.h"
#include "../../include/plot/plotIndustryParametric.h" // ★★★ 包含新头文件 ★★★

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/global_control.h>
#include <map>

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& industry_rpn_list,
    const std::vector<std::string>& industry_parametric_list, // ★★★ 新增参数 ★★★
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 清理输出容器
    out_points.clear();
    out_ranges.clear();

    const size_t count_pairs = implicit_rpn_pairs.size();
    const size_t count_direct = implicit_rpn_direct_list.size();
    const size_t total_implicit = count_pairs + count_direct; // 总普通隐函数
    const size_t total_industry_implicit = industry_rpn_list.size();
    const size_t total_industry_parametric = industry_parametric_list.size();

    // 总函数数量 = 普通隐函数 + 工业隐函数 + 工业参数方程
    const size_t total_functions = total_implicit + total_industry_implicit + total_industry_parametric;

    if (total_functions == 0) return;

    // 2. 配置 TBB 并发控制
    const auto thread_count = std::thread::hardware_concurrency();
    oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

    // 3. 准备结果队列和任务组
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(total_functions * 2);

    oneapi::tbb::task_group task_group;

    // 4. 计算世界坐标系参数
    double aspect_ratio = screen_width / screen_height;
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);

    Vec2 world_origin = {
        (centered_x_0 / zoom) + offset_x,
        (centered_y_0 / zoom) + offset_y
    };

    double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
    double world_per_pixel_y = -2.0 / (zoom * screen_height);

    // 5. 解析 RPN (仅针对普通隐函数，工业级函数在各自的处理函数内部解析)
    std::vector<AlignedVector<RPNToken>> implicit_programs;
    std::vector<AlignedVector<RPNToken>> implicit_programs_check;
    implicit_programs.reserve(total_implicit);
    implicit_programs_check.reserve(total_implicit);

    for (const auto& pair : implicit_rpn_pairs) {
        implicit_programs.emplace_back(parse_rpn(pair.first));
        implicit_programs_check.emplace_back(parse_rpn(pair.second));
    }

    for (const auto& rpn_str : implicit_rpn_direct_list) {
        auto tokens = parse_rpn(rpn_str);
        implicit_programs.push_back(tokens);
        implicit_programs_check.push_back(tokens);
    }

    // =========================================================
    // 6. 派发任务
    // =========================================================

    // 6.1 普通隐函数 (索引 0 ~ total_implicit-1)
    for (size_t i = 0; i < total_implicit; ++i) {
        task_group.run([&, i] {
            process_implicit_adaptive(
                &results_queue,
                world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                implicit_programs[i],
                implicit_programs_check[i],
                (unsigned int)i,
                offset_x, offset_y
            );
        });
    }

    // 6.2 工业级隐函数 (索引 total_implicit ~ +total_industry_implicit-1)
    for (size_t i = 0; i < total_industry_implicit; ++i) {
        task_group.run([&, i] {
            unsigned int func_idx = (unsigned int)(total_implicit + i);
            process_single_industry_function(
                &results_queue,
                industry_rpn_list[i],
                func_idx,
                world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                offset_x, offset_y,
                zoom
            );
        });
    }

    // 6.3 ★★★ 工业级参数方程 (索引接在后面) ★★★
    size_t parametric_start_idx = total_implicit + total_industry_implicit;
    for (size_t i = 0; i < total_industry_parametric; ++i) {
        task_group.run([&, i, parametric_start_idx] {
            unsigned int func_idx = (unsigned int)(parametric_start_idx + i);
            // 调用新模块的处理函数
            process_industry_parametric(
                &results_queue,
                industry_parametric_list[i],
                func_idx,
                world_origin, world_per_pixel_x, world_per_pixel_y,
                screen_width, screen_height,
                offset_x, offset_y
            );
        });
    }

    // 7. 收集结果
    std::map<unsigned int, std::vector<PointData>> sorted_results;

    for (size_t i = 0; i < total_functions; ++i) {
        FunctionResult res;
        results_queue.pop(res);
        sorted_results[res.function_index] = std::move(res.points);
    }

    // 8. 等待任务结束
    task_group.wait();

    // 9. 扁平化结果
    out_ranges.resize(total_functions);

    size_t total_points_count = 0;
    for (const auto& kv : sorted_results) total_points_count += kv.second.size();
    out_points.reserve(total_points_count);

    uint32_t current_start_index = 0;

    for (unsigned int idx = 0; idx < total_functions; ++idx) {
        auto it = sorted_results.find(idx);
        if (it != sorted_results.end()) {
            const auto& points_vec = it->second;
            uint32_t count = (uint32_t)points_vec.size();

            out_ranges[idx] = { current_start_index, count };

            if (count > 0) {
                out_points.insert(out_points.end(), points_vec.begin(), points_vec.end());
                current_start_index += count;
            }
        } else {
            out_ranges[idx] = { current_start_index, 0 };
        }
    }
}