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

// 【关键】定义原子索引，供 plotIndustry.cpp 链接使用
std::atomic<size_t> g_points_atomic_index{0};

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
        if (wasm_final_contiguous_buffer.capacity() < 5000000) {
            wasm_final_contiguous_buffer.reserve(5000000);
        }
    }

    // 2. 执行核心计算
    // plotIndustry 会在后台线程中不断更新 wasm_final_contiguous_buffer 并通知 JS
    // calculate_points_core 会计算普通隐函数，并更新 Range Buffer
    calculate_points_core(
        ordered_points,
        wasm_function_ranges_buffer,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 3. 最终合并与同步 (处理混合绘制的情况)
    if (has_industry) {
        // 此时 wasm_final_contiguous_buffer 已经被 plotIndustry 填充了数据
        // 这里的 size 是真实的工业函数点数
        size_t industry_count = wasm_final_contiguous_buffer.size();
        size_t implicit_count = ordered_points.size();

        // ====================================================================
        // 【核心修复】手动修正 Range Buffer
        // calculate_points_core 因为收到了空结果，把工业函数的点数设为了 0。
        // 我们必须把它改回来！
        // ====================================================================
        size_t industry_start_idx = implicit_rpn_pairs.size(); // 工业函数排在隐函数后面

        // 确保 Range Buffer 足够大
        if (wasm_function_ranges_buffer.size() <= industry_start_idx) {
            wasm_function_ranges_buffer.resize(industry_start_idx + 1);
        }

        // 强制更新工业函数的 Range：从 0 开始，长度为 industry_count
        // (因为我们在 plotIndustry 中是重置 index 从 0 开始写的)
        wasm_function_ranges_buffer[industry_start_idx] = {
            0,
            (uint32_t)industry_count
        };

        // 如果还有普通隐函数，需要把它们追加到后面，并修正它们的 Range
        if (implicit_count > 0) {
            // 扩容以容纳隐函数点
            wasm_final_contiguous_buffer.resize(industry_count + implicit_count);

            // 将隐函数点拷贝到尾部
            std::memcpy(
                wasm_final_contiguous_buffer.data() + industry_count,
                ordered_points.data(),
                implicit_count * sizeof(PointData)
            );

            // 修正普通隐函数的 Range (加上偏移量)
            // 普通隐函数在 ordered_points 里是从 0 开始的，现在被搬到了 industry_count 之后
            for (size_t i = 0; i < industry_start_idx; ++i) {
                wasm_function_ranges_buffer[i].start_index += (uint32_t)industry_count;
            }
        }
    }
    else {
        // 纯普通模式：直接覆盖 buffer
        if (!ordered_points.empty()) {
            wasm_final_contiguous_buffer.resize(ordered_points.size());
            std::memcpy(wasm_final_contiguous_buffer.data(), ordered_points.data(), ordered_points.size() * sizeof(PointData));
        } else {
            wasm_final_contiguous_buffer.clear();
        }
    }

    // 4. 结束标记
    g_is_calculating.store(false, std::memory_order_release);
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

// Getters
int get_data_version() { return g_industry_stage_version.load(std::memory_order_acquire); }
bool is_calculating() { return g_is_calculating.load(std::memory_order_acquire); }
uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

// Embind 绑定
EMSCRIPTEN_BINDINGS(my_module) {
    // 【关键】注册 VectorString，解决 "is not a constructor" 错误
    emscripten::register_vector<std::string>("VectorString");

    emscripten::function("start_calculation", &start_calculation_async);
    emscripten::function("get_data_version", &get_data_version);
    emscripten::function("is_calculating", &is_calculating);

    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else

// =========================================================
//        ↓↓↓ Native (Windows/Linux) 逻辑 ↓↓↓
// =========================================================

std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> final_points_aligned;
    AlignedVector<FunctionRange> final_ranges_aligned;

    // 1. 重置全局索引
    g_points_atomic_index.store(0);

    // 2. 内存预分配 (Native 端可以稍微大方点)
    if (wasm_final_contiguous_buffer.size() < 5000000) {
        wasm_final_contiguous_buffer.resize(5000000);
    }

    // 3. 核心计算
    calculate_points_core(
        final_points_aligned,
        final_ranges_aligned,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 4. 合并数据
    std::vector<PointData> merged_points;
    size_t industry_count = g_points_atomic_index.load();

    merged_points.reserve(final_points_aligned.size() + industry_count);

    // A. 插入普通隐函数点
    merged_points.insert(merged_points.end(), final_points_aligned.begin(), final_points_aligned.end());

    // B. 插入工业级函数点
    if (industry_count > 0) {
        if (industry_count <= wasm_final_contiguous_buffer.size()) {
            merged_points.insert(
                merged_points.end(),
                wasm_final_contiguous_buffer.begin(),
                wasm_final_contiguous_buffer.begin() + industry_count
            );
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
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::cout << "\n--- 准备隐式函数 ---\n";

        // 测试普通隐函数
        std::vector<std::string> implicit_rpn_direct_list = {
            "x x * y y * + 10 -"
        };
        if (!implicit_rpn_direct_list.empty()) {
            for(const auto& rpn_str : implicit_rpn_direct_list) {
                all_implicit_rpn_pairs.emplace_back(rpn_str, rpn_str);
            }
            std::cout << "已添加 " << implicit_rpn_direct_list.size() << " 个直接 RPN 输入。\n";
        }

        // 测试工业级函数
        std::vector<std::string> industry_rpn = { "y x tan -;0;0.1;10;2" };
        std::cout << "已准备 " << industry_rpn.size() << " 个工业级 RPN 函数。\n";

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