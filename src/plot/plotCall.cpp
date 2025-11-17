#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotParametric.h"
#include "../../include/plot/plotIndustry.h"
// Helper struct for explicit functions
struct ExplicitFunction {
    AlignedVector<RPNToken> rpn;
};

// Helper struct for parametric functions
struct ParametricFunction {
    AlignedVector<RPNToken> rpn_x;
    AlignedVector<RPNToken> rpn_y;
    double t_min;
    double t_max;
};

// Helper function to parse parametric function strings
ParametricFunction parse_parametric_string(const std::string& str) {
    std::vector<std::string> parts;
    std::string current_part;
    std::stringstream ss(str);
    while (std::getline(ss, current_part, ';')) {
        parts.push_back(current_part);
    }

    if (parts.size() != 4) {
        throw std::runtime_error("参数方程格式错误，必须是 'x_rpn;y_rpn;t_min;t_max'。收到: " + str);
    }

    try {
        return ParametricFunction{
            parse_rpn(parts[0]),
            parse_rpn(parts[1]),
            std::stod(parts[2]),
            std::stod(parts[3])
        };
    } catch (const std::exception& e) {
        throw std::runtime_error("解析参数方程时出错 (" + str + "): " + e.what());
    }
}

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    const std::vector<std::string>& industry_rpn_list, // <-- 新增参数
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    try {
        out_points.clear();
        out_ranges.clear();

        const auto thread_count = std::thread::hardware_concurrency();
        oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

        // --- 1. 计算世界坐标系参数 ---
        double aspect_ratio = screen_width / screen_height;
        double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
        double centered_y_0 = -(0.0 * 2.0 - 1.0);
        Vec2 world_origin = { (centered_x_0 / zoom) + offset_x, (centered_y_0 / zoom) + offset_y };
        double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
        double world_per_pixel_y = -2.0 / (zoom * screen_height);

        oneapi::tbb::task_group task_group;
        oneapi::tbb::combinable<ThreadCacheForTiling> thread_local_caches;

        // --- 2. 准备所有函数的RPN程序 ---
        std::vector<AlignedVector<RPNToken>> implicit_programs, implicit_programs_for_check;
        for(const auto& rpn_pair : implicit_rpn_pairs) {
            implicit_programs.push_back(parse_rpn(rpn_pair.first));
            implicit_programs_for_check.push_back(parse_rpn(rpn_pair.second));
        }

        std::vector<ExplicitFunction> explicit_programs;
        for(const auto& str : explicit_rpn_list) {
            explicit_programs.push_back({parse_rpn(str)});
        }

        std::vector<ParametricFunction> parametric_programs;
        for(const auto& str : parametric_rpn_list) {
            parametric_programs.push_back(parse_parametric_string(str));
        }

        // --- 3. 设置并发缓冲区和函数索引 ---
        const size_t total_functions = implicit_programs.size() + explicit_programs.size() + parametric_programs.size() + industry_rpn_list.size();
        std::vector<oneapi::tbb::concurrent_vector<PointData>> per_function_buffers(total_functions);

        const double world_x_start = world_origin.x;
        const double world_x_end = world_origin.x + screen_width * world_per_pixel_x;
        const double world_y_max = world_origin.y;
        const double world_y_min = world_origin.y + screen_height * world_per_pixel_y;
        const double max_dist_sq = std::pow(world_per_pixel_x, 2);
        const int max_depth = 15;
        const auto num_chunks = thread_count * 16;

        // --- 4. 并行调度所有绘图任务 ---

        // 隐函数
        for (size_t func_idx = 0; func_idx < implicit_programs.size(); ++func_idx) {
            task_group.run([=, &per_function_buffers, &thread_local_caches, &implicit_programs, &implicit_programs_for_check] {
                process_implicit_adaptive(
                    world_origin, world_per_pixel_x, world_per_pixel_y,
                    screen_width, screen_height,
                    implicit_programs[func_idx],
                    implicit_programs_for_check[func_idx],
                    (unsigned int)func_idx,
                    thread_local_caches,
                    per_function_buffers[func_idx]
                );
            });
        }

        // 显函数
        const unsigned int explicit_index_offset = (unsigned int)implicit_programs.size();
        for (unsigned int func_idx = 0; func_idx < explicit_programs.size(); ++func_idx) {
            const auto& fn_data = explicit_programs[func_idx];
            const double chunk_width = (world_x_end - world_x_start) / num_chunks;
            for (int i = 0; i < num_chunks; ++i) {
                task_group.run([=, &per_function_buffers] {
                    const unsigned int final_func_idx = func_idx + explicit_index_offset;
                    process_explicit_chunk(world_y_min, world_y_max, world_x_start + i * chunk_width, world_x_start + (i + 1) * chunk_width,
                                           fn_data.rpn, max_dist_sq, max_depth,
                                           per_function_buffers[final_func_idx], final_func_idx);
                });
            }
        }

        // 参数方程
        const unsigned int parametric_index_offset = explicit_index_offset + (unsigned int)explicit_programs.size();
        for (unsigned int func_idx = 0; func_idx < parametric_programs.size(); ++func_idx) {
            const auto& fn_data = parametric_programs[func_idx];
            const double t_chunk_width = (fn_data.t_max - fn_data.t_min) / num_chunks;
            for (int i = 0; i < num_chunks; ++i) {
                task_group.run([=, &per_function_buffers] {
                    const unsigned int final_func_idx = func_idx + parametric_index_offset;
                    process_parametric_chunk(world_y_min, world_y_max, world_x_start, world_x_end,
                                             fn_data.t_min + i * t_chunk_width, fn_data.t_min + (i + 1) * t_chunk_width,
                                             fn_data.rpn_x, fn_data.rpn_y,
                                             max_dist_sq, max_depth,
                                             per_function_buffers[final_func_idx], final_func_idx);
                });
            }
        }

        // (新增) 工业级高精度函数
        const unsigned int industry_index_offset = parametric_index_offset + (unsigned int)parametric_programs.size();
        for (unsigned int func_idx = 0; func_idx < industry_rpn_list.size(); ++func_idx) {
            task_group.run([=, &per_function_buffers] {
                const unsigned int final_func_idx = func_idx + industry_index_offset;
                // --- MODIFIED: 传递 offset 和 zoom 参数 ---
                process_single_industry_function(
                    per_function_buffers[final_func_idx],
                    industry_rpn_list[func_idx],
                    final_func_idx,
                    world_origin, world_per_pixel_x, world_per_pixel_y,
                    screen_width, screen_height,
                    offset_x, offset_y, zoom // <-- 传递新参数
                );
            });
        }

        task_group.wait();

        // --- 5. 合并所有线程的计算结果 ---
        size_t total_points = 0;
        for(const auto& buffer : per_function_buffers) {
            total_points += buffer.size();
        }
        out_points.reserve(total_points);
        out_ranges.reserve(total_functions);

        uint32_t current_start_index = 0;
        for (const auto& buffer : per_function_buffers) {
            uint32_t count = (uint32_t)buffer.size();
            out_ranges.push_back({current_start_index, count});
            if (count > 0) {
                out_points.insert(out_points.end(), buffer.begin(), buffer.end());
            }
            current_start_index += count;
        }

    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("C++ Calculation Error (std::exception): ") + e.what());
    }
    catch (...) {
        throw std::runtime_error("C++ Calculation Error: An unknown, non-standard exception was caught.");
    }
}