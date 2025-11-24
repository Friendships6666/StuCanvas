// --- 文件路径: src/main.cpp ---

#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h"

#include <atomic>
#include <thread>
#include <memory>
#include <cstring> // for memcpy
#include <algorithm> // for std::remove_if
#include <iostream>
#include <fstream>
#include <iomanip>

// =========================================================
//        ↓↓↓ 全局变量定义 ↓↓↓
// =========================================================

AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;
AlignedVector<PointData> g_preserved_points;

std::atomic<int> g_industry_stage_version{0};
std::atomic<bool> g_is_calculating{false};

double g_last_offset_x = 0.0;
double g_last_offset_y = 0.0;
double g_last_zoom = 0.0;

std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

// 引用 plotIndustry.cpp 中的原子索引，用于获取工业函数的实际点数
extern std::atomic<size_t> g_points_atomic_index;

void calculate_points_worker(
    std::vector<std::string> implicit_rpn_list,
    std::vector<std::string> industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 准备隐函数输入
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    AlignedVector<PointData> ordered_points; // 存放普通隐函数的计算结果

    // =========================================================
    // 【优化】WASM 端内存策略
    // =========================================================
    bool has_industry = !industry_rpn_list.empty();

    if (has_industry) {
        // 只有存在工业函数时，才重置索引并预留大内存
        g_points_atomic_index.store(0);

        // 预留空间，防止 plotIndustry 内部频繁 realloc
        // 注意：这里用 reserve 即可，plotIndustry 内部会处理 resize/swap
        if (wasm_final_contiguous_buffer.capacity() < 3096120) {
            wasm_final_contiguous_buffer.reserve(3096120);
        }
    } else {
        // 如果只有普通函数，不需要重置 g_points_atomic_index，
        // 也不需要预分配 100MB+ 内存，保持现状即可。
    }

    // 2. 执行核心计算
    // plotIndustry 会在后台线程中不断更新 wasm_final_contiguous_buffer 并通知 JS
    calculate_points_core(
        ordered_points,
        wasm_function_ranges_buffer,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 3. 最终合并与同步 (处理混合绘制的情况)
    if (has_industry) {
        // 工业模式下，wasm_final_contiguous_buffer 已经被 plotIndustry 填充了数据
        // 我们需要把 ordered_points (普通隐函数结果) 追加进去

        size_t industry_count = wasm_final_contiguous_buffer.size(); // 此时 buffer 已经是 swap 过的，size 是真实的
        size_t implicit_count = ordered_points.size();

        if (implicit_count > 0) {
            // 扩容以容纳隐函数点
            wasm_final_contiguous_buffer.resize(industry_count + implicit_count);

            // 将隐函数点拷贝到尾部
            std::memcpy(
                wasm_final_contiguous_buffer.data() + industry_count,
                ordered_points.data(),
                implicit_count * sizeof(PointData)
            );
        }

        // 此时 wasm_final_contiguous_buffer 包含了 [工业点 ... 隐函数点]
    }
    else {
        // 纯普通模式：直接覆盖 buffer
        if (!ordered_points.empty()) {
            wasm_final_contiguous_buffer.resize(ordered_points.size());
            std::memcpy(wasm_final_contiguous_buffer.data(), ordered_points.data(), ordered_points.size() * sizeof(PointData));
        } else {
            // 如果没有点，清空 buffer
            wasm_final_contiguous_buffer.clear();
        }
    }

    // 4. 结束标记
    g_is_calculating.store(false, std::memory_order_release);
    g_industry_stage_version.fetch_add(1, std::memory_order_release);
}

#else

// =========================================================
//        ↓↓↓ Native (Windows/Linux) 修复逻辑 ↓↓↓
// =========================================================

// 【定义】原子索引，用于跟踪实际写入了多少个点
// plotIndustry.cpp 中使用 extern 引用此变量
std::atomic<size_t> g_points_atomic_index{0};

std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> final_points_aligned;
    AlignedVector<FunctionRange> final_ranges_aligned;

    // 1. 重置全局索引 (每次计算前归零)
    g_points_atomic_index.store(0);

    // 2. 【核心修复】使用 resize 而不是 reserve
    // 这确保 vector 认为自己有 500万大小，后续的指针写入是合法的覆盖操作
    if (!industry_rpn_list.empty()) {
        if (wasm_final_contiguous_buffer.size() < 3096120) {
            wasm_final_contiguous_buffer.resize(3096120);
        }
    }

    // 3. 核心计算 (Task Group 并行：同时计算普通隐函数和工业级函数)
    calculate_points_core(
        final_points_aligned,
        final_ranges_aligned,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 4. 合并数据
    std::vector<PointData> merged_points;

    // 读取工业级函数实际写入了多少个点
    size_t industry_count = g_points_atomic_index.load();

    merged_points.reserve(final_points_aligned.size() + industry_count);

    // A. 插入普通隐函数点 (来自 final_points_aligned)
    merged_points.insert(merged_points.end(), final_points_aligned.begin(), final_points_aligned.end());

    // B. 插入工业级函数点 (来自全局 Buffer)
    if (industry_count > 0) {
        // 安全检查
        if (industry_count <= wasm_final_contiguous_buffer.size()) {
            // 直接拷贝有效数据段
            merged_points.insert(
                merged_points.end(),
                wasm_final_contiguous_buffer.begin(),
                wasm_final_contiguous_buffer.begin() + industry_count
            );
        } else {
            std::cerr << "[Error] Industry count (" << industry_count << ") exceeds buffer size!" << std::endl;
        }
    }

    std::cout << "[Main] Merged " << final_points_aligned.size() << " implicit points and "
              << industry_count << " industry points." << std::endl;

    std::vector<FunctionRange> merged_ranges;
    if (!final_ranges_aligned.empty()) {
        merged_ranges.assign(final_ranges_aligned.begin(), final_ranges_aligned.end());
    }

    return { merged_points, merged_ranges };
}

int main() {
    try {
        // --- 1. 准备所有函数列表 ---
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::cout << "\n--- 准备隐式函数 ---\n";

        // 【新增】添加一个标准的圆方程作为普通隐函数测试
        // x^2 + y^2 - 1 = 0  => RPN: x 2 pow y 2 pow + 1 -
        std::vector<std::string> implicit_rpn_direct_list = {

        };

        if (!implicit_rpn_direct_list.empty()) {
            for(const auto& rpn_str : implicit_rpn_direct_list) {
                // 普通隐函数 Check RPN 和 Main RPN 通常设为一样
                all_implicit_rpn_pairs.emplace_back(rpn_str, rpn_str);
            }
            std::cout << "已添加 " << implicit_rpn_direct_list.size() << " 个直接 RPN 输入。\n";
        }

        // 工业级函数
        std::vector<std::string> industry_rpn = { "y x tan -;0;0.1;10;2" };
        std::cout << "已准备 " << industry_rpn.size() << " 个工业级 RPN 函数。\n";

        // --- 2. 设置所有绘图共享的视图属性 ---
        double offset_x = 0, offset_y = 0;
        double zoom = 0.1; // 50px = 1 unit
        double screen_width = 2560, screen_height = 1600;

        std::cout << "View: " << screen_width << "x" << screen_height << " Zoom: " << zoom << std::endl;

        // --- 3. 执行统一的并行计算 ---
        std::cout << "\n--- Native EXE: 开始计算所有函数... ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        auto results = calculate_points_for_native(
            all_implicit_rpn_pairs,
            industry_rpn,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
        const auto& final_points = results.first;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---\n";
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总共生成了 " << final_points.size() << " 个点。" << std::endl;

        // --- 4. 保存结果 ---
        if (!final_points.empty()) {
            std::cout << "\n正在保存到 points.txt..." << std::endl;
            std::ofstream output_file("points.txt");
            if (!output_file.is_open()) {
                std::cerr << "错误: 无法打开文件 points.txt 进行写入！" << std::endl;
                return 1;
            }

            output_file << std::fixed << std::setprecision(12);
            for (const auto& p : final_points) {
                output_file << p.position.x << " " << p.position.y << " " << p.function_index << "\n";
            }
            output_file.close();
            std::cout << "保存成功！" << std::endl;
        } else {
            std::cout << "警告：没有生成任何点，不保存文件。" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "\n!!! 程序遇到严重错误 !!!\n";
        std::cerr << "错误详情: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#endif // __EMSCRIPTEN__