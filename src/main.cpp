#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>   // 💡 新增：用于文件操作
#include <string>    // 💡 新增：用于字符串处理
#include <iomanip>

#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/plot/plotCall.h"

// 引用全局显存 Buffer (确保与 plotCall.cpp 中使用的一致)
extern std::vector<PointData> wasm_final_contiguous_buffer;
extern std::vector<FunctionRange> wasm_function_ranges_buffer;

// =========================================================
// 💡 辅助函数：导出当前阶段的点数据
// =========================================================
void ExportStagePoints(int stage_index) {
    std::string filename = "points" + std::to_string(stage_index) + ".txt";
    std::ofstream outFile(filename);

    if (!outFile.is_open()) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return;
    }

    // 设置固定浮点精度，方便后续在 Python 或 Excel 中分析
    outFile << std::fixed << std::setprecision(6);

    for (const auto& pt : wasm_final_contiguous_buffer) {
        // 格式: x y function_index
        outFile << pt.position.x << " "
                << pt.position.y << " "
                << pt.function_index << "\n";
    }

    outFile.close();
    std::cout << "[Disk] Stage " << stage_index << " points exported to: " << filename
              << " (" << wasm_final_contiguous_buffer.size() << " points)" << std::endl;
}

int main() {
    try {
        std::cout << "=== GeoEngine: Single Creation & Calculation Test ===" << std::endl;

        // 1. 初始化几何图对象
        GeometryGraph graph;

        // 2. 锁定配置视图属性
        double screen_width = 2560.0;
        double screen_height = 1600.0;
        double zoom = 0.1;
        double aspect_ratio = screen_width / screen_height;

        graph.view.screen_width = screen_width;
        graph.view.screen_height = screen_height;
        graph.view.zoom = zoom;
        graph.view.wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        graph.view.wppy = -2.0 / (zoom * screen_height);
        graph.view.world_origin = {
            0.0 - (screen_width * 0.5) * graph.view.wppx,
            0.0 - (screen_height * 0.5) * graph.view.wppy
        };

        // 3. 构造场景 (Factory 会自动 mark_as_seed)
        std::cout << "[Step 1] Creating Point A, B and Segment AB..." << std::endl;

        uint32_t idA = GeoFactory::AddFreePoint(graph, "-5.0", "3.2");
        uint32_t idB = GeoFactory::AddFreePoint(graph, "2.0", "4.5");
        uint32_t idL = GeoFactory::AddSegment(graph, idA, idB);

        // 4. 执行计算引擎
        std::cout << "[Step 2] Running Calculation Engine..." << std::endl;

        // 第一次调用，由于 ViewState 发生变化（从默认到设定值），
        // 内部 Ping-Pong 机制会识别为 Viewport 模式并触发全量 Plot
        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph
        );

        // =========================================================
        // 💡 调用辅助函数导出结果
        // =========================================================
        ExportStagePoints(1);

        // 5. 验证结果输出
        auto& nodeA = graph.get_node_by_id(idA);
        auto& nodeB = graph.get_node_by_id(idB);
        auto& nodeL = graph.get_node_by_id(idL);

        std::cout << "\n[Results Verification]" << std::endl;
        std::cout << "Point A Pos: (" << nodeA.result.x << ", " << nodeA.result.y << ")" << std::endl;
        std::cout << "Point B Pos: (" << nodeB.result.x << ", " << nodeB.result.y << ")" << std::endl;
        std::cout << "Line Valid:  " << (nodeL.result.check_f(ComputedResult::VALID) ? "YES" : "NO") << std::endl;

        std::cout << "\n[Memory Stats]" << std::endl;
        std::cout << "Total Active Nodes:  " << graph.node_pool.size() << std::endl;
        std::cout << "Total Render Points: " << wasm_final_contiguous_buffer.size() << std::endl;
        std::cout << "Total Draw Commands: " << wasm_function_ranges_buffer.size() << std::endl;

        // 验证绘制顺序 (ID 序)
        std::cout << "\n[Draw Order List]" << std::endl;
        for (size_t i = 0; i < wasm_function_ranges_buffer.size(); ++i) {
            std::cout << "Command [" << i << "] Offset: " << wasm_function_ranges_buffer[i].start_index << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL FAILURE: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}