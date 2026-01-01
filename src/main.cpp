#include "../pch.h"
#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/plot/plotCall.h"
#include "../include/functions/lerp.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>

// 辅助导出函数
void ExportPoints(const std::string& filename, const AlignedVector<PointData>& buffer, const AlignedVector<FunctionRange>& ranges, const std::vector<uint32_t>& draw_order) {
    std::ofstream outfile(filename);
    outfile << "# WebGPU Plotter Export: " << filename << "\n";
    outfile << "# Total Buffer Size: " << buffer.size() << "\n";
    outfile << "# [X_Clip] [Y_Clip] [Func_ID]\n";
    outfile << std::fixed << std::setprecision(6);
    for (const auto& pt : buffer) {
        outfile << pt.position.x << " " << pt.position.y << " " << pt.function_index << "\n";
    }
    outfile.close();
}

int main() {
    try {
        // =========================================================
        // 1. 初始化视图参数 (依照题目要求)
        // =========================================================
        double screen_width = 2560.0;
        double screen_height = 1600.0;
        double offset_x = 0.0;
        double offset_y = 0.0;
        double zoom = 0.1;

        double aspect_ratio = screen_width / screen_height;

        // 计算渲染核心需要的世界坐标系常数
        // 逻辑：屏幕中心映射为 (offset_x, offset_y)
        double wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        double wppy = -2.0 / (zoom * screen_height); // Y轴向上为正，屏幕坐标向下，所以为负

        // 计算屏幕左上角的世界坐标原点
        Vec2 world_origin = {
            offset_x - (screen_width * 0.5) * wppx,
            offset_y - (screen_height * 0.5) * wppy
        };

        ViewState view{};
        view.screen_width = screen_width;
        view.screen_height = screen_height;
        view.offset_x = offset_x;
        view.offset_y = offset_y;
        view.zoom = zoom;
        view.world_origin = world_origin;
        view.wppx = wppx;
        view.wppy = wppy;


        using namespace GeoFactory;
        GeometryGraph graph;

        // 创建 P1(-5,0), P2(5,0), P3(0,5)
        uint32_t A = CreatePoint(graph, {-5.0}, {0.0});
        uint32_t B = CreatePoint(graph, {5.0}, {0.0});
        uint32_t C = CreatePoint(graph, {0.0}, {5.0});
        uint32_t circumCircle = CreateCircleThreePoints(graph, A, B, C);

        std::vector<uint32_t> draw_order = { A, B, C, circumCircle };

        // =========================================================
        // 2. 第一轮：全局渲染 (Global Update)
        // =========================================================
        std::cout << "[Step 1] Initializing Global Render..." << std::endl;

        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph.node_pool,
            draw_order,
            {},    // global模式下传空
            view,
            true   // is_global_update = true
        );

        std::cout << "Initial Buffer Size: " << wasm_final_contiguous_buffer.size() << std::endl;
        ExportPoints("points1.txt", wasm_final_contiguous_buffer, wasm_function_ranges_buffer, draw_order);

        // =========================================================
        // 3. 第二轮：局部更新 (Local Incremental Update)
        // =========================================================
        std::cout << "\n[Step 2] Moving Point A to (-10.0, 0.0)..." << std::endl;

        std::vector<uint32_t> dirty_nodes_0 = graph.SolveFrame();

        std::cout << "Dirty nodes detected by SolveFrame00000: ";
        for(uint32_t id : dirty_nodes_0) std::cout << id << " ";
        std::cout << std::endl;

        // 修改 A 点坐标并标记脏
        UpdateFreePoint(graph, A, {-10.0}, {0.0});

        // 核心步骤：运行计算图求解器
        // 它会发现 A 脏了，进而发现依赖 A 的 circumCircle 也脏了
        std::vector<uint32_t> dirty_nodes = graph.SolveFrame();
        auto& posA = std::get<Data_Point>(graph.node_pool[A].data);
        auto& circle = std::get<Data_Circle>(graph.node_pool[circumCircle].data);
        std::cout << "[Check] Point A is now: (" << posA.x << ", " << posA.y << ")" << std::endl;
        std::cout << "[Check] Circle Center: (" << circle.cx << ", " << circle.cy << ") R=" << circle.radius << std::endl;

        std::cout << "Dirty nodes detected by SolveFrame: ";
        for(uint32_t id : dirty_nodes) std::cout << id << " ";
        std::cout << std::endl;

        // 执行局部渲染：只重新计算 dirty_nodes 并在 Buffer 末尾追加
        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph.node_pool,
            draw_order,
            dirty_nodes,
            view,
            false  // is_global_update = false (局部模式)
        );

        std::cout << "Final Buffer Size (after append): " << wasm_final_contiguous_buffer.size() << std::endl;
        ExportPoints("points2.txt", wasm_final_contiguous_buffer, wasm_function_ranges_buffer, draw_order);

        // =========================================================
        // 4. 详细日志分析
        // =========================================================
        std::cout << "\n--- Ring Buffer Analysis ---" << std::endl;
        for(size_t i = 0; i < wasm_function_ranges_buffer.size(); ++i) {
            uint32_t node_id = draw_order[i];
            auto& r = wasm_function_ranges_buffer[i];
            bool is_dirty = false;
            for(uint32_t d : dirty_nodes) if(d == node_id) is_dirty = true;

            std::cout << "Node ID " << node_id
                      << (is_dirty ? " [UPDATED] " : " [STAYED]  ")
                      << " Offset=" << std::setw(6) << r.start_index
                      << " Count=" << std::setw(6) << r.point_count << std::endl;
        }

        std::cout << "\nResults saved. Verify points1.txt (Old) and points2.txt (Combined)." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}