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

// =========================================================
//        ↓↓↓ 全局变量 (WASM 与 JS 共享的数据桥梁) ↓↓↓
// =========================================================

// 存储最终点数据的连续缓冲区
AlignedVector<PointData> wasm_final_contiguous_buffer;
// 存储每个函数点数范围的缓冲区
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

// 原子状态变量 (用于 JS 轮询)
std::atomic<int> g_industry_stage_version{0}; // 版本号：每次数据更新 +1
std::atomic<bool> g_is_calculating{false};    // 状态位：是否正在计算

// 全局任务组，用于管理后台计算线程
std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

// =========================================================
//        ↓↓↓ 后台 Worker 函数 (在独立线程运行) ↓↓↓
// =========================================================
void calculate_points_worker(
    std::vector<std::string> implicit_rpn_list,
    std::vector<std::string> industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 准备隐函数输入格式
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    AlignedVector<PointData> ordered_points;

    // 注意：wasm_function_ranges_buffer 是全局的，calculate_points_core 会修改它
    // 在多线程环境下，这里实际上是安全的，因为此时 JS 只是在轮询 version，
    // 只有 version 变了才会去读 buffer。

    // 2. 执行核心计算 (耗时操作)
    // plotIndustry.cpp 内部会根据细分阶段，中途更新全局 buffer 并增加 version
    calculate_points_core(
        ordered_points,
        wasm_function_ranges_buffer,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 3. 计算彻底完成后的最终同步
    // 确保隐式函数的结果和最终状态被正确写入
    if (!ordered_points.empty()) {
        wasm_final_contiguous_buffer.resize(ordered_points.size());
        std::memcpy(wasm_final_contiguous_buffer.data(), ordered_points.data(), ordered_points.size() * sizeof(PointData));
    }

    // 4. 更新状态标记
    g_is_calculating.store(false, std::memory_order_release);
    // 增加版本号，触发 JS 最后一次读取
    g_industry_stage_version.fetch_add(1, std::memory_order_release);
}

// =========================================================
//        ↓↓↓ 导出给 JS 的 API ↓↓↓
// =========================================================

/**
 * @brief 启动异步计算 (非阻塞)
 * JS 调用此函数后会立即返回，计算在后台线程进行。
 */
void start_calculation_async(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 如果有旧任务，先等待其清理 (防止重入导致的数据竞争)
    if (g_global_task_group) {
        g_global_task_group->wait();
    } else {
        g_global_task_group = std::make_unique<oneapi::tbb::task_group>();
    }

    // 重置状态
    g_industry_stage_version.store(0, std::memory_order_release);
    g_is_calculating.store(true, std::memory_order_release);

    // 启动后台任务 (按值捕获所有参数，确保线程安全)
    g_global_task_group->run([=]() {
        calculate_points_worker(
            implicit_rpn_list,
            industry_rpn_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
    });
}

// 获取当前数据版本号 (JS 轮询用)
int get_data_version() {
    return g_industry_stage_version.load(std::memory_order_acquire);
}

// 检查是否正在计算
bool is_calculating() {
    return g_is_calculating.load(std::memory_order_acquire);
}

// 获取缓冲区指针和大小
uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

// Embind 绑定
EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");

    // 新的异步控制 API
    emscripten::function("start_calculation", &start_calculation_async);
    emscripten::function("get_data_version", &get_data_version);
    emscripten::function("is_calculating", &is_calculating);

    // 数据访问 API
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else // --- Native EXE Version (用于本地调试) ---

// 本地版保持不变，用于生成 points.txt 调试
std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> final_points_aligned;
    AlignedVector<FunctionRange> final_ranges_aligned;

    calculate_points_core(
        final_points_aligned,
        final_ranges_aligned,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    return {
        std::vector<PointData>(final_points_aligned.begin(), final_points_aligned.end()),
        std::vector<FunctionRange>(final_ranges_aligned.begin(), final_ranges_aligned.end())
    };
}
int main() {
    try {
        // --- 1. 准备所有函数列表 ---
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::cout << "\n--- 准备隐式函数 ---\n";
        std::vector<std::string> implicit_rpn_direct_list = {}; // 100 identical circles
        if (!implicit_rpn_direct_list.empty()) {
            for(const auto& rpn_str : implicit_rpn_direct_list) {
                all_implicit_rpn_pairs.emplace_back(rpn_str, rpn_str);
            }
            std::cout << "已添加 " << implicit_rpn_direct_list.size() << " 个直接 RPN 输入。\n";
        }

        std::vector<std::string> explicit_rpn = {};
        std::vector<std::string> parametric_rpn = {};
        std::vector<std::string> industry_rpn = { "y x tan -;0;0.1;10;2" };
        std::cout << "已准备 " << industry_rpn.size() << " 个工业级 RPN 函数。\n";

        // --- 2. 设置所有绘图共享的视图属性 ---
        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 2560, screen_height = 1600;

        // --- 3. 执行统一的并行计算 ---
        std::cout << "\n--- Native EXE: 开始计算所有函数... ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        auto results = calculate_points_for_native(
            all_implicit_rpn_pairs,
            industry_rpn,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
        const auto& final_points = results.first; // 此向量已包含混合坐标

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---\n";
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总共生成了 " << final_points.size() << " 个点。" << std::endl;

        // --- 4. 将混合坐标系的点直接保存到 points.txt ---
        std::cout << "\n正在将 [混合坐标系] 的结果保存到 points.txt..." << std::endl;
        std::ofstream output_file("points.txt");
        if (!output_file.is_open()) {
            std::cerr << "错误: 无法打开文件 points.txt 进行写入！" << std::endl;
            return 1;
        }

        if (!final_points.empty()) {
            output_file << std::fixed << std::setprecision(12);
            // 直接写入，不再需要进行任何转换
            for (const auto& p : final_points) {
                output_file << p.position.x << " " << p.position.y << " " << p.function_index << "\n";
            }
        }

        output_file.close();
        std::cout << "保存成功！" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\n!!! 程序遇到严重错误 !!!\n";
        std::cerr << "错误详情: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#endif // __EMSCRIPTEN__