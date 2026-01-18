#include <iostream>
#include <vector>
#include <chrono>

#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/plot/plotCall.h"



int main() {
    try {
        std::cout << "=== GeoEngine: Single Creation & Calculation Test ===" << std::endl;

        // 1. 初始化几何图对象 (内部持有 ViewState, Pool, LUT)
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

        // 3. 构造场景 (Factory 内部会自动调用 mark_as_seed)
        std::cout << "[Step 1] Creating Point A, B and Segment AB..." << std::endl;

        // 创建点 A(-5.0, 3.2) - 内部产生 2个标量 + 1个点
        uint32_t idA = GeoFactory::AddFreePoint(graph, "-5.0", "3.2");
        // 创建点 B(2.0, 4.5) - 内部产生 2个标量 + 1个点
        uint32_t idB = GeoFactory::AddFreePoint(graph, "2.0", "4.5");
        // 创建线段 L(A, B) - 内部产生 1个线段
        uint32_t idL = GeoFactory::AddSegment(graph, idA, idB);

        // 4. 执行计算引擎
        // 💡 这里不再传入任何 ID 列表，不再手动维护绘制顺序
        // 💡 模式设为 Incremental，内核会自动消费 Factory 产生的种子
        std::cout << "[Step 2] Running Calculation Engine..." << std::endl;

        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph,
            RenderUpdateMode::Incremental
        );

        // 5. 验证结果
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

        // 验证后来者后画
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