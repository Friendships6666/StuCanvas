#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#ifdef _WIN32

#endif
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

void calculate_points_for_wasm(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    AlignedVector<PointData> ordered_absolute_points;
    calculate_points_core(
        ordered_absolute_points,
        wasm_function_ranges_buffer,
        implicit_rpn_list, explicit_rpn_list, parametric_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );

    wasm_final_contiguous_buffer.resize(ordered_absolute_points.size());
    for (size_t i = 0; i < ordered_absolute_points.size(); ++i) {
        const auto& absolute_point = ordered_absolute_points[i];
        auto& local_point = wasm_final_contiguous_buffer[i];

        local_point.position.x = absolute_point.position.x - offset_x;
        local_point.position.y = absolute_point.position.y - offset_y;
        local_point.function_index = absolute_point.function_index;
    }
}

uintptr_t get_points_ptr() {
    return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data());
}
size_t get_points_size() {
    return wasm_final_contiguous_buffer.size();
}

uintptr_t get_function_ranges_ptr() {
    return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data());
}
size_t get_function_ranges_size() {
    return wasm_function_ranges_buffer.size();
}

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");
    emscripten::value_object<Vec2>("Vec2")
        .field("x", &Vec2::x)
        .field("y", &Vec2::y);
    emscripten::value_object<PointData>("PointData")
        .field("position", &PointData::position)
        .field("function_index", &PointData::function_index);
    emscripten::value_object<FunctionRange>("FunctionRange")
        .field("start_index", &FunctionRange::start_index)
        .field("point_count", &FunctionRange::point_count);

    emscripten::function("calculate_points", &calculate_points_for_wasm);
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else // --- Native EXE Version ---

// ====================================================================
//  FIXED: 确保此函数的签名与之前的修改一致
// ====================================================================
std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& parametric_rpn_list,
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
        explicit_rpn_list, parametric_rpn_list,
        offset_x, offset_y, zoom, screen_width, screen_height
    );
    return {
        std::vector<PointData>(final_points_aligned.begin(), final_points_aligned.end()),
        std::vector<FunctionRange>(final_ranges_aligned.begin(), final_ranges_aligned.end())
    };
}

int main() {
    try{
        std::cout << "\n--- CAS 符号化简与 RPN 生成测试 ---\n";

        std::vector<std::string> implicit_mathjson_list = {
            R"([
  "Equal",
  "y",
  [
    "Divide",
    1,
    "x"
  ]
])"
        };

        std::cout << "输入 MathJSON: \n" << implicit_mathjson_list[0] << std::endl;

        auto start_cas_time = std::chrono::high_resolution_clock::now();

        std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
        for (const auto& json_str : implicit_mathjson_list) {
            auto ast = CAS::JsonAdapter::parse_json_to_ast_simdjson(json_str);
            // ====================================================================
            //  FIXED: 明确指定 constant_fold 的命名空间
            // ====================================================================
            implicit_rpn_pairs.push_back(CAS::GraphicSimplify::constant_fold(ast));
        }

        auto end_cas_time = std::chrono::high_resolution_clock::now();
        auto cas_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_cas_time - start_cas_time);

        std::cout << "\n生成 Normal RPN: " << implicit_rpn_pairs[0].first << std::endl;
        std::cout << "生成 Check RPN:   " << implicit_rpn_pairs[0].second << std::endl;
        std::cout << "CAS 处理耗时: " << cas_duration.count() << " 微秒\n";
        std::cout << "--- CAS 测试结束 ---\n\n";

        // ====================================================================
        //  FIXED: 移除了旧的、未使用的 implicit_rpn 变量
        // ====================================================================
        std::vector<std::string> explicit_rpn = {};
        std::vector<std::string> parametric_rpn = {};

        double offset_x = 0.0;
        double offset_y = 0.0;
        double zoom = 0.1;
        double screen_width = 2560.0;
        double screen_height = 1600.0;

        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        // ====================================================================
        //  FIXED: 传递了正确的变量 (implicit_rpn_pairs)
        // ====================================================================
        auto results = calculate_points_for_native(
            implicit_rpn_pairs,
            explicit_rpn, parametric_rpn,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
        const auto& final_points = results.first;
        const auto& final_ranges = results.second;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---" << std::endl;
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总共生成了 " << final_points.size() << " 个点。" << std::endl;

        std::cout << "\n正在将结果保存到 points.txt (x y index 格式)..." << std::endl;
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

    } catch (const std::exception& e) {
        std::cerr << "\n!!! 程序遇到严重错误 !!!\n";
        std::cerr << "错误详情: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

#endif // __EMSCRIPTEN__