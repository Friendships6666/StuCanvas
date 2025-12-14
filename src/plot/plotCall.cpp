// --- 文件路径: src/plot/plotCall.cpp ---

#include "../../pch.h"
#include "../../include/plot/plotCall.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/plot/plotImplicit.h"
#include "../../include/plot/plotExplicit.h" // ★★★ 包含显函数头文件 ★★★
#include "../../include/plot/plotIndustry.h"
#include "../../include/plot/plotIndustryParametric.h"

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/task_group.h>
#include <oneapi/tbb/global_control.h>
#include <map>
#include <algorithm> // for std::min, std::max

void calculate_points_core(
    AlignedVector<PointData>& out_points,
    AlignedVector<FunctionRange>& out_ranges,
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& explicit_rpn_list, // <--- 新增
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
    const size_t count_explicit = explicit_rpn_list.size(); // <--- 新增

    const size_t total_implicit = count_pairs + count_direct;
    // 显函数索引接在普通隐函数之后
    const size_t explicit_start_idx = total_implicit;
    const size_t total_normal = total_implicit + count_explicit;

    const size_t total_industry_implicit = industry_rpn_list.size();
    const size_t total_industry_parametric = industry_parametric_list.size();

    // 总函数数量
    const size_t total_functions = total_normal + total_industry_implicit + total_industry_parametric;

    if (total_functions == 0) return;

    // 2. 配置 TBB
    const auto thread_count = std::thread::hardware_concurrency();
    oneapi::tbb::global_control control(oneapi::tbb::global_control::max_allowed_parallelism, thread_count);

    // 3. 准备队列和任务组
    oneapi::tbb::concurrent_bounded_queue<FunctionResult> results_queue;
    results_queue.set_capacity(total_functions * 2);

    oneapi::tbb::task_group task_group;

    // 4. 计算世界坐标系参数
    double aspect_ratio = screen_width / screen_height;
    // 屏幕左上角在归一化设备坐标中通常是 (-1, 1) 或者根据具体投影矩阵
    // 这里沿用原有的逻辑推导
    double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
    double centered_y_0 = -(0.0 * 2.0 - 1.0);

    Vec2 world_origin = {
        (centered_x_0 / zoom) + offset_x,
        (centered_y_0 / zoom) + offset_y
    };

    double world_per_pixel_x = (2.0 * aspect_ratio) / (zoom * screen_width);
    double world_per_pixel_y = -2.0 / (zoom * screen_height);

    // --- 计算屏幕边界的世界坐标 (用于显函数) ---
    // x_start: 屏幕左侧 (pixel 0)
    // x_end:   屏幕右侧 (pixel width)
    double x_start_world = world_origin.x;
    double x_end_world = world_origin.x + screen_width * world_per_pixel_x;

    // y_min/max: 屏幕顶部和底部
    // 注意 wppy 通常是负数（屏幕Y向下，世界Y向上），所以 origin.y 是顶部(Top/Max)，加 height*wppy 是底部(Bottom/Min)
    double y_top_world = world_origin.y;
    double y_bottom_world = world_origin.y + screen_height * world_per_pixel_y;

    double y_min_world = std::min(y_top_world, y_bottom_world);
    double y_max_world = std::max(y_top_world, y_bottom_world);

    // 5. 解析 RPN (普通隐函数)
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

    // 6. 解析 RPN (普通显函数) ★★★
    std::vector<AlignedVector<RPNToken>> explicit_programs;
    explicit_programs.reserve(count_explicit);
    for (const auto& rpn_str : explicit_rpn_list) {
        explicit_programs.emplace_back(parse_rpn(rpn_str));
    }

    // =========================================================
    // 7. 派发任务
    // =========================================================

    // 7.1 普通隐函数
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

    // 7.2 ★★★ 普通显函数 ★★★
    for (size_t i = 0; i < count_explicit; ++i) {
        task_group.run([&, i] {
            unsigned int func_idx = (unsigned int)(explicit_start_idx + i);

            // 显函数使用 concurrent_vector 来接收结果
            oneapi::tbb::concurrent_vector<PointData> points;

            // 调用高性能显函数处理模块
            // 注意：这里 offset_x/y 的处理逻辑是在 plotExplicit 内部通过 x_start_world 蕴含的
            // 最终输出的点是 World 坐标，需要减去 offset 转换为相对坐标供前端渲染？
            // 不，根据其他模块逻辑，PointData.position 通常存储的是“相对于 offset 的坐标”或者“世界坐标”。
            // 检查 plotImplicit: intersection.x -= offset_x;
            // 检查 plotIndustry: pt.position.x = cast_to_double(task.x) - offset_x;
            // 所以，Points 输出必须是 (WorldPos - Offset)。

            // plotExplicit 内部生成的是 World 坐标 (x_start + i*step)。
            // 我们需要在 plotExplicit 内部或者这里减去 offset。
            // 为了保持 plotExplicit 的通用性（纯数学计算），我们最好修改 plotExplicit 让其输出 World 坐标，
            // 但目前的 plotExplicit 实现直接写入 all_points。

            // === 修正策略 ===
            // plotExplicit.cpp 中: raw_buffer[i].position.x = x; (这是世界坐标)
            // 我们需要在存入 all_points 前减去 offset。
            // 考虑到性能，最好在 plotExplicit 内部传入 offset_x/y 进行减法。
            // 但如果不修改 plotExplicit 接口，我们需要在这里后处理。
            // 鉴于 plotExplicit 是新写的，我们假设它生成的是世界坐标。
            // 为了统一，我们暂时在下面做一次变换，或者修改 plotExplicit。
            //
            // 让我们采用最高效的方法：修改 plotExplicit 的调用，传入 offset，让其内部直接减。
            // 但上一步 plotExplicit.h 没加 offset 参数。
            //
            // 变通：我们在 x_start_world 传入时就传入 (x_start_world - offset_x)?
            // 不行，x 需要代入 RPN 计算 y。RPN 里的 x 是世界坐标。
            //
            // 所以：必须在 PointData 写入时减去 offset。
            // 为了不再次修改 plotExplicit.h (假设已定稿)，我们在这里做一个后处理。
            // 或者，由于我们是在 task 中运行，这点后处理开销可以接受。

            process_explicit_chunk(
                y_min_world, y_max_world,
                x_start_world, x_end_world,
                explicit_programs[i],
                points,
                func_idx,
                screen_width
            );

            // 将 concurrent_vector 转为 vector 并进行坐标偏移修正
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

    // 7.3 工业级隐函数
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

    // 7.4 工业级参数方程
    size_t parametric_start_idx = industry_start_idx + total_industry_implicit;
    for (size_t i = 0; i < total_industry_parametric; ++i) {
        task_group.run([&, i, parametric_start_idx] {
            unsigned int func_idx = (unsigned int)(parametric_start_idx + i);
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

    // 8. 收集结果
    std::map<unsigned int, std::vector<PointData>> sorted_results;
    for (size_t i = 0; i < total_functions; ++i) {
        FunctionResult res;
        results_queue.pop(res);
        sorted_results[res.function_index] = std::move(res.points);
    }

    // 9. 等待结束
    task_group.wait();

    // 10. 扁平化
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