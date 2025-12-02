// --- 文件路径: src/main.cpp ---

#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h"

#include <atomic>
#include <thread>
#include <memory>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>

// =========================================================
//        ↓↓↓ 全局变量定义 ↓↓↓
// =========================================================

// 与 JS 交换数据的全局缓冲区
AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

// 状态控制
std::atomic<int> g_industry_stage_version{0};
std::atomic<bool> g_is_calculating{false};
std::atomic<size_t> g_points_atomic_index{0};

// 任务组
std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

// 【配置】初始内存设置
// 起步 20万点 (约 4.8MB)，保证普通函数秒开，不会有大内存初始化的延迟
constexpr size_t INITIAL_BUFFER_CAPACITY = 200000;

// =========================================================
//        ↓↓↓ 核心计算逻辑 (共享，内存智能管理版) ↓↓↓
// =========================================================

/**
 * @brief 统一的计算入口，处理内存分配策略和数据合并。
 * 此函数现在位于 ifdef 之外，供 Native 和 WASM 共同调用。
 */
void calculate_points_internal(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 预处理隐函数 (String -> Pair<RPN, CheckRPN>)
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    bool has_industry = !industry_rpn_list.empty();

    if (has_industry) {
        // ==========================================
        // 路径 A: 工业模式 (混合渲染)
        // ==========================================
        g_points_atomic_index.store(0);

        // 1. 初始内存准备
        // 如果当前容量极小，扩展到基准线；如果已经很大（复用上一帧），则保留，避免反复 alloc
        if (wasm_final_contiguous_buffer.capacity() < INITIAL_BUFFER_CAPACITY) {
            wasm_final_contiguous_buffer.reserve(INITIAL_BUFFER_CAPACITY);
        }
        // resize 到 capacity，让 plotIndustry 可以通过 data() 指针直接写入
        // 注意：这里没有 resize 到 500万，而是按需增长
        if (wasm_final_contiguous_buffer.size() < wasm_final_contiguous_buffer.capacity()) {
            wasm_final_contiguous_buffer.resize(wasm_final_contiguous_buffer.capacity());
        }

        // 2. 计算普通函数 (放入临时 vector，支持自动扩容)
        AlignedVector<PointData> ordered_points;
        if (!implicit_rpn_pairs.empty()) {
            // 给普通函数也预留一点空间，避免 realloc
            ordered_points.reserve(INITIAL_BUFFER_CAPACITY / 2);
        }

        // calculate_points_core 内部逻辑：
        // - 普通函数 -> push_back 到 ordered_points (自动扩容)
        // - 工业函数 -> 通过 plotIndustry 直接写 wasm_final_contiguous_buffer (原子索引)
        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            industry_rpn_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 3. 数据合并 (Data Merge)
        // 此时：
        // - 工业函数数据已在 wasm_final_contiguous_buffer [0 ... g_points_atomic_index)
        // - 普通函数数据在 ordered_points

        size_t industry_count = g_points_atomic_index.load();
        size_t implicit_count = ordered_points.size();
        size_t total_needed = industry_count + implicit_count;

        // 确保全局 Range buffer 足够大
        size_t industry_start_idx = implicit_rpn_pairs.size();
        if (wasm_function_ranges_buffer.size() <= industry_start_idx) {
            wasm_function_ranges_buffer.resize(industry_start_idx + 1);
        }

        // 确保最终 buffer 足够大 (可能需要再次扩容以容纳追加的普通点)
        if (wasm_final_contiguous_buffer.size() < total_needed) {
            wasm_final_contiguous_buffer.resize(total_needed);
        }

        // 将普通函数数据拷贝到尾部
        if (implicit_count > 0) {
            std::memcpy(
                wasm_final_contiguous_buffer.data() + industry_count,
                ordered_points.data(),
                implicit_count * sizeof(PointData)
            );

            // 修正普通函数的 Range (因为它们被移动到了 industry 后面)
            for(size_t i=0; i<industry_start_idx; ++i) {
                wasm_function_ranges_buffer[i].start_index += (uint32_t)industry_count;
            }
        }

        // 设置工业函数的 Range
        wasm_function_ranges_buffer[industry_start_idx] = { 0, (uint32_t)industry_count };

        // 截断到实际使用的总大小，这一步至关重要，告诉 JS 真实数据量
        wasm_final_contiguous_buffer.resize(total_needed);
    }
    else {
        // ==========================================
        // 路径 B: 纯普通模式 (极速响应)
        // ==========================================
        // 此时不涉及原子索引和多线程随机写，使用局部 vector 管理内存最高效
        AlignedVector<PointData> ordered_points;

        // 1. 智能预留
        ordered_points.reserve(INITIAL_BUFFER_CAPACITY);

        std::vector<std::string> empty_industry;

        // 2. 核心计算
        // 这里如果生成了千万个点，ordered_points 会自动 grow，不会崩溃
        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            empty_industry,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 3. 结果交付
        // 将结果 assign 给全局 buffer
        // assign 会自动处理 resize，如果 ordered_points 很大，这里会分配相应内存
        // 这是一个 O(N) 的拷贝操作，但对于 1000万点 (240MB) 来说，内存拷贝也是毫秒级的
        wasm_final_contiguous_buffer.assign(ordered_points.begin(), ordered_points.end());
    }
}

// =========================================================
//        ↓↓↓ WASM 编译分支 ↓↓↓
// =========================================================
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

// =========================================================
//        ↓↓↓ 导出接口 (保持接口稳定) ↓↓↓
// =========================================================

void calculate_implicit_sync(
    const std::vector<std::string>& implicit_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 停止后台任务，防止冲突
    if (g_global_task_group) g_global_task_group->wait();
    g_is_calculating.store(false);

    std::vector<std::string> empty_industry;
    calculate_points_internal(implicit_rpn_list, empty_industry, offset_x, offset_y, zoom, screen_width, screen_height);
}

void start_industry_async(
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    if (g_global_task_group) g_global_task_group->wait();
    else g_global_task_group = std::make_unique<oneapi::tbb::task_group>();

    g_industry_stage_version.store(0, std::memory_order_release);
    g_is_calculating.store(true, std::memory_order_release);

    std::vector<std::string> empty_implicit;

    g_global_task_group->run([=]() {
        calculate_points_internal(empty_implicit, industry_rpn_list, offset_x, offset_y, zoom, screen_width, screen_height);

        g_is_calculating.store(false, std::memory_order_release);
        g_industry_stage_version.fetch_add(1, std::memory_order_release);
    });
}

// Getters
int get_data_version() { return g_industry_stage_version.load(std::memory_order_acquire); }
bool is_calculating() { return g_is_calculating.load(std::memory_order_acquire); }
uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

// Embind
EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");

    emscripten::function("calculate_implicit_sync", &calculate_implicit_sync);
    emscripten::function("start_calculation", &start_industry_async); // 兼容旧名
    emscripten::function("start_industry_async", &start_industry_async);

    emscripten::function("get_data_version", &get_data_version);
    emscripten::function("is_calculating", &is_calculating);

    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else

// =========================================================
//        ↓↓↓ Native EXE 逻辑 (Windows/Linux 调试用) ↓↓↓
// =========================================================

std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 将 pair 转换回 list 字符串，以适配 internal 接口
    std::vector<std::string> implicit_strs;
    implicit_strs.reserve(implicit_rpn_pairs.size());
    for(const auto& p : implicit_rpn_pairs) {
        implicit_strs.push_back(p.first);
    }

    // 调用统一的内部逻辑
    calculate_points_internal(
        implicit_strs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 将全局 buffer 转换为 pair 返回 (Native 接口要求)
    return {
        std::vector<PointData>(wasm_final_contiguous_buffer.begin(), wasm_final_contiguous_buffer.end()),
        std::vector<FunctionRange>(wasm_function_ranges_buffer.begin(), wasm_function_ranges_buffer.end())
    };
}

int main() {
    try {
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::cout << "\n--- 准备隐式函数 ---\n";

        // 测试普通隐函数
        std::vector<std::string> implicit_rpn_direct_list = {};

        if (!implicit_rpn_direct_list.empty()) {
            for(const auto& rpn_str : implicit_rpn_direct_list) {
                all_implicit_rpn_pairs.emplace_back(rpn_str, rpn_str);
            }
        }

        // 测试工业级函数 (此处为空)
        std::vector<std::string> industry_rpn = {     "y x tan -;0;0.1;10;2"};

        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 2560, screen_height = 1600;

        std::cout << "View: " << screen_width << "x" << screen_height << " Zoom: " << zoom << std::endl;
        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        // 调用封装好的 native 接口
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
            if (!output_file.is_open()) return 1;
            output_file << std::fixed << std::setprecision(12);
            for (const auto& p : final_points) {
                output_file << p.position.x << " " << p.position.y << " " << p.function_index << "\n";
            }
            output_file.close();
            std::cout << "保存成功！" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#endif // __EMSCRIPTEN__