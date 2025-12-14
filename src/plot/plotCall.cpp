// --- 文件路径: src/plot/plotCall.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotExplicit.h"
#include "../../include/plot/plotParametric.h" // ★★★ 包含普通参数方程头文件 ★★★
#include "../../include/plot/plotIndustry.h"
#include "../../include/plot/plotIndustryParametric.h"

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/global_control.h>
#include <map>
#include <algorithm> // for std::min, std::max
#include <sstream>

// 辅助：解析 "x_rpn;y_rpn;t_min;t_max" 格式
struct ExplicitParamConfig {
    std::string x_rpn;
    std::string y_rpn;
    double t_min = 0.0;
    double t_max = 1.0;
};

ExplicitParamConfig parse_explicit_param_config(const std::string& input) {
    ExplicitParamConfig config;
    std::vector<std::string> parts;
    std::stringstream ss(input);
    std::string item;
    while(std::getline(ss, item, ';')) {
        parts.push_back(item);
    }
    if (parts.size() >= 2) {
        config.x_rpn = parts[0];
        config.y_rpn = parts[1];
    }
    if (parts.size() >= 4) {
        try {
            config.t_min = std::stod(parts[2]);
            config.t_max = std::stod(parts[3]);
        } catch(...) {}
    }
    return config;
}

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& explicit_parametric_list, // <--- 新增参数实现
    const std::vector<std::string>& industry_rpn_list,
    const std::vector<std::string>& industry_parametric_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 清理输出容器
    out_points.clear();
    out_ranges.clear();

    const size_t count_pairs = implicit_rpn_pairs.size();
    const size_t count_direct = implicit_rpn_direct_list.size();
    const size_t count_explicit = explicit_rpn_list.size();
    const size_t count_exp_parametric = explicit_parametric_list.size(); // <--- 新增计数

    // 索引计算
    const size_t total_implicit = count_pairs + count_direct;
    const size_t explicit_start_idx = total_implicit;
    const size_t exp_parametric_start_idx = explicit_start_idx + count_explicit; // <--- 新增起始索引

    // 普通模式总数
    const size_t total_normal = exp_parametric_start_idx + count_exp_parametric;

    const size_t total_industry_implicit = industry_rpn_list.size();
    const size_t total_industry_parametric = industry_parametric_list.size();

    // 总函数数量
    const size_t total_functions = total_normal + total_industry_implicit + total_industry_parametric;

    if (total_functions == 0) return;

    // 2. 配置 TBB 并发控制
    const auto thread_count = std::thread::hardware_concurrency();
    oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

    // 3. 准备结果队列和任务组
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(total_functions * 2);

    oneapi::tbb::task_group task_group;

    // 4. 计算世界坐标系参数 (用于显函数和隐函数)
    double aspect_ratio = screen_width / screen_height;
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);

    Vec2 world_origin = {
        (centered_x_0 / zoom) + offset_x,
        (centered_y_0 / zoom) + offset_y
    };

    double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
    double world_per_pixel_y = -2.0 / (zoom * screen_height);

    // 计算屏幕边界的世界坐标
    double x_start_world = world_origin.x;
    double x_end_world = world_origin.x + screen_width * world_per_pixel_x;
    double y_top_world = world_origin.y;
    double y_bottom_world = world_origin.y + screen_height * world_per_pixel_y;
    double y_min_world = std::min(y_top_world, y_bottom_world);
    double y_max_world = std::max(y_top_world, y_bottom_world);

    // 5. 解析 RPN (预处理)

    // 5.1 隐函数
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

    // 5.2 显函数
    std::vector<AlignedVector<RPNToken>> explicit_programs;
    explicit_programs.reserve(count_explicit);
    for (const auto& rpn_str : explicit_rpn_list) {
        explicit_programs.emplace_back(parse_rpn(rpn_str));
    }

    // 5.3 普通参数方程 (需要解析配置字符串)
    struct ParsedParametric {
        AlignedVector<RPNToken> x_prog;
        AlignedVector<RPNToken> y_prog;
        double t_min;
        double t_max;
    };
    std::vector<ParsedParametric> exp_parametric_data;
    exp_parametric_data.reserve(count_exp_parametric);
    for (const auto& config_str : explicit_parametric_list) {
        auto conf = parse_explicit_param_config(config_str);
        exp_parametric_data.push_back({
            parse_rpn(conf.x_rpn),
            parse_rpn(conf.y_rpn),
            conf.t_min,
            conf.t_max
        });
    }

    // =========================================================
    // 6. 派发任务
    // =========================================================

    // 6.1 普通隐函数
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

    // 6.2 普通显函数
    for (size_t i = 0; i < count_explicit; ++i) {
        task_group.run([&, i] {
            unsigned int func_idx = (unsigned int)(explicit_start_idx + i);
            oneapi::tbb::concurrent_vector<PointData> points;

            process_explicit_chunk(
                y_min_world, y_max_world,
                x_start_world, x_end_world,
                explicit_programs[i],
                points,
                func_idx,
                screen_width
            );

            // 坐标修正：转换为相对坐标
            std::vector<PointData> res_vec;
            res_vec.reserve(points.size());
            for(const auto& p : points) {
                PointData new_p = p;
                new_p.position.x -= offset_x;
                new_p.position.y -= offset_y;
                res_vec.push_back(new_p);
            }
            results_queue.push({func_idx, std::move(res_vec)});
        });
    }

    // 6.3 ★★★ 普通参数方程 ★★★
    for (size_t i = 0; i < count_exp_parametric; ++i) {
        task_group.run([&, i, exp_parametric_start_idx] {
            unsigned int func_idx = (unsigned int)(exp_parametric_start_idx + i);
            const auto& data = exp_parametric_data[i];

            oneapi::tbb::concurrent_vector<PointData> points;

            // 调用高性能参数方程处理模块
            process_parametric_chunk(
                data.x_prog,
                data.y_prog,
                data.t_min,
                data.t_max,
                points,
                func_idx
            );

            // 坐标修正：转换为相对坐标
            // process_parametric_chunk 生成的是纯数学坐标 (World)，需要减去 offset
            std::vector<PointData> res_vec;
            res_vec.reserve(points.size());
            for(const auto& p : points) {
                PointData new_p = p;
                new_p.position.x -= offset_x;
                new_p.position.y -= offset_y;
                res_vec.push_back(new_p);
            }
            results_queue.push({func_idx, std::move(res_vec)});
        });
    }

    // 索引偏移量更新
    size_t industry_start_idx = total_normal;

    // 6.4 工业级隐函数
    for (size_t i = 0; i < total_industry_implicit; ++i) {
        task_group.run([&, i, industry_start_idx] {
            unsigned int func_idx = (unsigned int)(industry_start_idx + i);
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

    // 6.5 工业级参数方程
    size_t industry_parametric_start_idx = industry_start_idx + total_industry_implicit;
    for (size_t i = 0; i < total_industry_parametric; ++i) {
        task_group.run([&, i, industry_parametric_start_idx] {
            unsigned int func_idx = (unsigned int)(industry_parametric_start_idx + i);
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

    // 8. 等待结束
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