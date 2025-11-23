#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h"
#ifdef _WIN32

#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

/**
 * @brief (WASM 导出) 为 WebAssembly 环境计算所有函数图像的点。
 *
 * 注意：此函数生成的点数据包含两种坐标系：
 * 1. 普通隐函数 (Implicit): 绝对世界坐标 (World Space)。前端 Shader 需要减去 Offset 并乘以 Zoom。
 * 2. 工业级函数 (Industry): 屏幕像素坐标 (Screen Pixel Space)。前端 Shader 应直接映射到 Clip Space，忽略变换。
 *
 * 索引顺序：[隐函数 0...N] -> [工业函数 0...M]
 */
void calculate_points_for_wasm(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
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

    AlignedVector<PointData> ordered_points;

    // 2. 调用核心计算
    // calculate_points_core 内部会先处理 implicit_rpn_pairs，再处理 industry_rpn_list
    // 因此 ordered_points 中的索引也是这个顺序。
    calculate_points_core(
        ordered_points,
        wasm_function_ranges_buffer,
        implicit_rpn_pairs,
        industry_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    // 3. 填充 WASM 缓冲区
    // 这里直接拷贝坐标，不进行任何转换。
    // Implicit 点是 World 坐标，Industry 点是 Pixel 坐标。
    wasm_final_contiguous_buffer.resize(ordered_points.size());
    // 使用 memcpy 加速拷贝 (PointData 结构简单时安全，否则用循环)
    if (!ordered_points.empty()) {
        std::memcpy(wasm_final_contiguous_buffer.data(), ordered_points.data(), ordered_points.size() * sizeof(PointData));
    }
}

uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");

    // 导出函数
    emscripten::function("calculate_points", &calculate_points_for_wasm);
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else // --- Native EXE Version ---

// 函数定义保持不变
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
        std::vector<std::string> industry_rpn = { "y x tan -;0" };
        std::cout << "已准备 " << industry_rpn.size() << " 个工业级 RPN 函数。\n";

        // --- 2. 设置所有绘图共享的视图属性 ---
        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 1280, screen_height = 876;

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