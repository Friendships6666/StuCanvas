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
//        ↓↓↓ 全局变量定义 (容器隔离) ↓↓↓
// =========================================================

// 1. 普通隐函数/参数方程使用的容器
AlignedVector<PointData> g_implicit_points;
AlignedVector<FunctionRange> g_implicit_ranges;

// 2. 工业级函数使用的容器
AlignedVector<PointData> g_industry_points;
AlignedVector<FunctionRange> g_industry_ranges;

// 工业级函数专用的原子索引，用于多线程安全写入 g_industry_points
std::atomic<size_t> g_industry_atomic_index{0};

// 保留点容器 (用于某些特定的持久化需求，防止链接错误)
AlignedVector<PointData> g_preserved_points;

// 状态控制
std::atomic<int> g_industry_stage_version{0};
std::atomic<bool> g_is_calculating{false};

double g_last_offset_x = 0.0;
double g_last_offset_y = 0.0;
double g_last_zoom = 0.0;

std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

// =========================================================
//        ↓↓↓ WASM Worker 逻辑 ↓↓↓
// =========================================================

void calculate_points_worker(
    std::vector<std::string> implicit_rpn_list,
    std::vector<std::string> industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // ----------------------------------------------------------------
    // 阶段 1: 计算普通隐函数 (Implicit Functions)
    // ----------------------------------------------------------------
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        // 目前简单将同一字符串用于计算和检查，实际应用中可能是不同的
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    // 调用 plotCall 模块，结果直接写入 g_implicit_points / g_implicit_ranges
    calculate_points_core(
        g_implicit_points,
        g_implicit_ranges,
        implicit_rpn_pairs,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // ----------------------------------------------------------------
    // 阶段 2: 计算工业级函数 (Industrial Functions)
    // ----------------------------------------------------------------
    bool has_industry = !industry_rpn_list.empty();

    // 无论是否有工业函数，都先重置工业相关的状态
    g_industry_atomic_index.store(0);

    if (has_industry) {
        // 预分配内存：工业函数产生的点数通常较多
        if (g_industry_points.capacity() < 5000000) {
            g_industry_points.reserve(5000000);
        }
        // plotIndustry.cpp 需要直接通过 data() 指针写入，所以这里不 resize，
        // 而是由内部逻辑管理，或者在内部 resize。
        // 但为了安全，我们确保它不为空且有容量。
        // 注意：plotIndustry.cpp 内部会处理 resize。

        // 调整 Ranges 数组大小
        // 工业函数的索引是接在隐函数之后的。
        // 例如：隐函数有 2 个 (idx 0, 1)，工业函数有 1 个 (idx 2)。
        // g_industry_ranges 需要足够大以容纳 idx 2。
        size_t implicit_count = implicit_rpn_list.size();
        size_t total_count = implicit_count + industry_rpn_list.size();

        if (g_industry_ranges.size() < total_count) {
            g_industry_ranges.resize(total_count);
        }

        // 准备视图参数
        double aspect_ratio = screen_width / screen_height;
        double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
        double centered_y_0 = -(0.0 * 2.0 - 1.0);
        Vec2 world_origin = { (centered_x_0 / zoom) + offset_x, (centered_y_0 / zoom) + offset_y };
        double wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        double wppy = -2.0 / (zoom * screen_height);

        // 使用一个伪队列来满足接口要求（实际并未完全使用，因为内部改为了直接写全局 buffer）
        oneapi::tbb::concurrent_bounded_queue<FunctionResult> dummy_queue;

        // 启动计算
        // 这里可以并行派发多个工业函数，但考虑到工业函数内部已经是高并发的，
        // 串行派发函数级别通常足够吃满 CPU。
        for (size_t i = 0; i < industry_rpn_list.size(); ++i) {
            // 全局唯一的函数索引
            unsigned int final_func_idx = (unsigned int)(implicit_count + i);

            process_single_industry_function(
                &dummy_queue,
                industry_rpn_list[i],
                final_func_idx,
                world_origin, wppx, wppy,
                screen_width, screen_height,
                offset_x, offset_y, zoom
            );
        }
    } else {
        // 如果没有工业函数，清空相关缓冲区
        // g_industry_points.clear(); // 不一定要 clear，只要 index 为 0 即可
        g_industry_ranges.clear();
    }

    // ----------------------------------------------------------------
    // 阶段 3: 完成标记
    // ----------------------------------------------------------------
    g_is_calculating.store(false, std::memory_order_release);
    // 增加版本号，通知 JS 前端读取新数据
    g_industry_stage_version.fetch_add(1, std::memory_order_release);
}

// 导出给 JS 的启动函数
void start_calculation_async(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    if (g_global_task_group) {
        g_global_task_group->wait();
    } else {
        g_global_task_group = std::make_unique<oneapi::tbb::task_group>();
    }

    // 重置状态
    g_industry_stage_version.store(0, std::memory_order_release);
    g_is_calculating.store(true, std::memory_order_release);

    // 启动后台任务 (按值捕获 vector，发生拷贝是安全的)
    g_global_task_group->run([=]() {
        calculate_points_worker(
            implicit_rpn_list,
            industry_rpn_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
    });
}

// =========================================================
//        ↓↓↓ Getters (暴露给 JS) ↓↓↓
// =========================================================

int get_data_version() { return g_industry_stage_version.load(std::memory_order_acquire); }
bool is_calculating() { return g_is_calculating.load(std::memory_order_acquire); }

// --- 组 1: 普通隐函数数据 ---
uintptr_t get_implicit_points_ptr() { return reinterpret_cast<uintptr_t>(g_implicit_points.data()); }
size_t get_implicit_points_size() { return g_implicit_points.size(); }
uintptr_t get_implicit_ranges_ptr() { return reinterpret_cast<uintptr_t>(g_implicit_ranges.data()); }
size_t get_implicit_ranges_size() { return g_implicit_ranges.size(); }

// --- 组 2: 工业级函数数据 ---
uintptr_t get_industry_points_ptr() { return reinterpret_cast<uintptr_t>(g_industry_points.data()); }
size_t get_industry_points_size() {
    // 返回实际写入原子计数器的值，而不是 vector 的 capacity 或 size
    // 确保 JS 只读取有效数据
    size_t idx = g_industry_atomic_index.load(std::memory_order_acquire);
    // 安全钳位，防止超出实际分配的 vector 大小
    return std::min(idx, g_industry_points.size());
}
uintptr_t get_industry_ranges_ptr() { return reinterpret_cast<uintptr_t>(g_industry_ranges.data()); }
size_t get_industry_ranges_size() { return g_industry_ranges.size(); }

// Embind 绑定
EMSCRIPTEN_BINDINGS(my_module) {
    // 注册 VectorString，解决 JS 传递数组问题
    emscripten::register_vector<std::string>("VectorString");

    emscripten::function("start_calculation", &start_calculation_async);
    emscripten::function("get_data_version", &get_data_version);
    emscripten::function("is_calculating", &is_calculating);

    // 暴露新的分离 API
    emscripten::function("get_implicit_points_ptr", &get_implicit_points_ptr);
    emscripten::function("get_implicit_points_size", &get_implicit_points_size);
    emscripten::function("get_implicit_ranges_ptr", &get_implicit_ranges_ptr);
    emscripten::function("get_implicit_ranges_size", &get_implicit_ranges_size);

    emscripten::function("get_industry_points_ptr", &get_industry_points_ptr);
    emscripten::function("get_industry_points_size", &get_industry_points_size);
    emscripten::function("get_industry_ranges_ptr", &get_industry_ranges_ptr);
    emscripten::function("get_industry_ranges_size", &get_industry_ranges_size);
}

#else

// =========================================================
//        ↓↓↓ Native (Windows/Linux) 逻辑 ↓↓↓
// =========================================================

// Native 端的模拟函数，用于调试或生成文件
std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 计算隐函数
    calculate_points_core(
        g_implicit_points,
        g_implicit_ranges,
        implicit_rpn_pairs,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 2. 计算工业函数
    g_industry_atomic_index.store(0);
    if (!industry_rpn_list.empty()) {
        if (g_industry_points.size() < 5000000) g_industry_points.resize(5000000);

        double aspect_ratio = screen_width / screen_height;
        double centered_x_0 = (0.0 * 2.0 - 1.0) * aspect_ratio;
        double centered_y_0 = -(0.0 * 2.0 - 1.0);
        Vec2 world_origin = { (centered_x_0 / zoom) + offset_x, (centered_y_0 / zoom) + offset_y };
        double wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        double wppy = -2.0 / (zoom * screen_height);

        oneapi::tbb::concurrent_bounded_queue<FunctionResult> dummy_queue;

        size_t implicit_count = implicit_rpn_pairs.size();
        if (g_industry_ranges.size() < implicit_count + industry_rpn_list.size()) {
            g_industry_ranges.resize(implicit_count + industry_rpn_list.size());
        }

        for (size_t i = 0; i < industry_rpn_list.size(); ++i) {
            process_single_industry_function(
                &dummy_queue,
                industry_rpn_list[i],
                (unsigned int)(implicit_count + i),
                world_origin, wppx, wppy,
                screen_width, screen_height,
                offset_x, offset_y, zoom
            );
        }
    }

    // 3. 合并结果用于返回 (Native端合并方便输出文件)
    std::vector<PointData> merged_points;
    merged_points.reserve(g_implicit_points.size() + g_industry_atomic_index.load());

    // 添加隐函数点
    merged_points.insert(merged_points.end(), g_implicit_points.begin(), g_implicit_points.end());

    // 添加工业函数点
    size_t ind_count = g_industry_atomic_index.load();
    if (ind_count > 0 && ind_count <= g_industry_points.size()) {
        merged_points.insert(merged_points.end(), g_industry_points.begin(), g_industry_points.begin() + ind_count);
    }

    // 合并 Ranges
    std::vector<FunctionRange> merged_ranges;
    merged_ranges.insert(merged_ranges.end(), g_implicit_ranges.begin(), g_implicit_ranges.end());
    // 工业 Ranges 可能需要修正 offset，但作为调试输出暂时忽略

    return { merged_points, merged_ranges };
}

int main() {
    try {
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::cout << "\n--- 准备隐式函数 ---\n";

        // 测试普通隐函数
        std::vector<std::string> implicit_rpn_direct_list = {
            "x 2 pow y 2 pow + 10 -"
        };
        for(const auto& rpn_str : implicit_rpn_direct_list) {
            all_implicit_rpn_pairs.emplace_back(rpn_str, rpn_str);
        }

        // 测试工业级函数
        std::vector<std::string> industry_rpn = { };

        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 2560, screen_height = 1600;

        std::cout << "View: " << screen_width << "x" << screen_height << " Zoom: " << zoom << std::endl;

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
        }

    } catch (const std::exception& e) {
        std::cerr << "\n!!! 程序遇到严重错误 !!!\n";
        std::cerr << "错误详情: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#endif // __EMSCRIPTEN__