#include "../pch.h"
#include "../include/graph/GeoGraph.h"
#include "../include/plot/plotCall.h"
#include "../include/functions/lerp.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>

// 全局模拟 Buffer (对应 WASM 里的 SharedArrayBuffer)
AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

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

        ViewState view;
        view.screen_width = screen_width;
        view.screen_height = screen_height;
        view.offset_x = offset_x;
        view.offset_y = offset_y;
        view.zoom = zoom;
        view.world_origin = world_origin;
        view.wppx = wppx;
        view.wppy = wppy;

        // =========================================================
        // 2. 构建几何依赖图 (两个自由点 + 一条线段)
        // =========================================================
        GeometryGraph graph;

        // --- A. 创建自由点 P1 (2, 3) ---
        uint32_t idP1 = graph.allocate_node();
        GeoNode& nodeP1 = graph.node_pool[idP1];
        nodeP1.render_type = GeoNode::RenderType::Point;
        nodeP1.data = Data_Point{ 0, 0 };
        nodeP1.rank = 0;

        // --- B. 创建自由点 P2 (5, 6) ---
        uint32_t idP2 = graph.allocate_node();
        GeoNode& nodeP2 = graph.node_pool[idP2];
        nodeP2.render_type = GeoNode::RenderType::Point;
        nodeP2.data = Data_Point{ 2, 2 };
        nodeP2.rank = 0;

        // --- C. 创建线段 L (依赖 P1, P2) ---
        uint32_t idL = graph.allocate_node();
        GeoNode& nodeL = graph.node_pool[idL];
        nodeL.render_type = GeoNode::RenderType::Line;
        nodeL.parents = { idP1, idP2 };
        nodeL.data = Data_Line{ idP1, idP2, false }; // false = 线段
        nodeL.rank = 1;

        // 注册反向依赖 (用于未来可能的局部更新测试)
        graph.node_pool[idP1].children.push_back(idL);
        graph.node_pool[idP2].children.push_back(idL);

        // =========================================================
        // 3. 执行全局渲染计算
        // =========================================================
        std::cout << "--- Starting Global Render ---" << std::endl;

        // 绘制顺序：先画线，后画点 (画家算法)
        std::vector<uint32_t> draw_order = { idL, idP1, idP2 };

        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph.node_pool,
            draw_order,
            {},    // 全局模式，dirty_nodes 为空
            view,
            true   // is_global_update = true (重置内存指针)
        );

        // =========================================================
        // 4. 将结果导出到 points.txt
        // =========================================================
        std::ofstream outfile("points.txt");
        if (!outfile.is_open()) {
            throw std::runtime_error("Could not open points.txt for writing.");
        }

        outfile << "# WebGPU Plotter Debug Result\n";
        outfile << "# View: Offset(" << offset_x << "," << offset_y << ") Zoom=" << zoom << "\n";
        outfile << "# Total points: " << wasm_final_contiguous_buffer.size() << "\n";
        outfile << "# [X_Clip] [Y_Clip] [Func_ID]\n";
        outfile << std::fixed << std::setprecision(6);

        // 遍历所有生成的点数据
        for (const auto& pt : wasm_final_contiguous_buffer) {
            outfile << pt.position.x << " " 
                    << pt.position.y << " " 
                    << pt.function_index << "\n";
        }

        outfile.close();

        // 控制台输出摘要
        std::cout << "Render Success!" << std::endl;
        std::cout << "Points saved to points.txt: " << wasm_final_contiguous_buffer.size() << std::endl;
        
        for(size_t i=0; i<wasm_function_ranges_buffer.size(); ++i) {
            auto& r = wasm_function_ranges_buffer[i];
            std::cout << "Obj ID " << draw_order[i] 
                      << ": Start=" << r.start_index 
                      << ", Count=" << r.point_count << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}