#include "../pch.h"
#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
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

        GeometryGraph graph;

        // 1. 利用工厂极简创建
        // 自动处理了：内存申请、数据填入、Rank计算、父子依赖注册
        uint32_t p1 = GeoFactory::CreateFreePoint(graph, 0.0, 0.0);
        uint32_t p2 = GeoFactory::CreateFreePoint(graph, 2.0, 2.0);

        // 创建线段
        uint32_t line = GeoFactory::CreateLine(graph, p1, p2, false);

        // 创建中点
        uint32_t mid = GeoFactory::CreateMidpoint(graph, p1, p2);




        // 3. 渲染顺序 (画家算法)
        std::vector<uint32_t> draw_order = {  mid, p1, p2 , line };



        // =========================================================
        // 3. 执行全局渲染计算
        // =========================================================
        std::cout << "--- Starting Global Render ---" << std::endl;



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