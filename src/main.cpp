#include "../pch.h"
#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/plot/plotCall.h"
#include "../include/functions/lerp.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>



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

        // =========================================================
        // 1. 创建圆及其依赖项
        // =========================================================
        // 创建圆心 (0, 0)
        uint32_t p_center = CreatePoint(graph, {0.0}, {3.0});
        // 创建圆，半径 5.0
        uint32_t circle = CreateCircle(graph, p_center, {6.0});

        // =========================================================
        // 2. 创建线段及其依赖项
        // =========================================================
        // 创建线段端点 A (-10, -5)
        uint32_t pL1 = CreatePoint(graph, {-10.0}, {-5.0});
        // 创建线段端点 B (10, 5)
        uint32_t pL2 = CreatePoint(graph, {10.0}, {5.0});
        // 创建线段 (is_infinite = false)
        uint32_t segment = CreateLine(graph, pL1, pL2, false);

        // =========================================================
        // 3. 创建交点 (图解法)
        // =========================================================
        // 初始猜测点设在 (4.5, 2.2) 附近，这接近圆与该直线的其中一个交点
        // 目标列表：{circle, segment}
        uint32_t inter_pt = CreateIntersectionPoint(graph, {6.0}, {2.2}, {circle, segment});

        // =========================================================
        // 4. 定义渲染顺序 (画家算法)
        // =========================================================
        std::vector<uint32_t> draw_order = {
            circle,      // 底层：圆
            segment,     // 中层：线段
            p_center,    // 顶层：各个控制点
            pL1,
            pL2,
            inter_pt     // 交点放在最上面，方便观察吸附效果
        };


        std::cout << "Graph construction successful." << std::endl;


        // =========================================================
        // 3. 执行全局渲染计算
        // =========================================================
        std::cout << "--- Starting Global Render ---" << std::endl;



        // 1. 记录开始时间点
        auto start_time = std::chrono::high_resolution_clock::now();

        // 2. 执行核心计算函数
        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            graph.node_pool,
            draw_order,
            {},    // 全局模式，dirty_nodes 为空
            view,
            true   // is_global_update = true
        );


        // 3. 记录结束时间点
        auto end_time = std::chrono::high_resolution_clock::now();

        // 4. 计算差值并转换为毫秒 (double 类型可以保留小数位，如 1.2345 ms)
        std::chrono::duration<double, std::milli> ms_double = end_time - start_time;

        // 5. 输出结果

        std::cout << "绘制核心计算总耗时: " << std::fixed << std::setprecision(4)
                  << ms_double.count() << " ms" << std::endl;


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

        std::cout << "Render Success!" << std::endl;
        std::cout << "Points saved to points.txt: " << wasm_final_contiguous_buffer.size() << std::endl;

        // 定义类型转换辅助 Lambda
        auto GetTypeStr = [](GeoNode::RenderType type) {
            switch (type) {
                case GeoNode::RenderType::Point:      return "[Point]";
                case GeoNode::RenderType::Line:       return "[Line]";
                case GeoNode::RenderType::Circle:     return "[Circle]";
                case GeoNode::RenderType::Explicit:   return "[Explicit Func]";
                case GeoNode::RenderType::Parametric: return "[Parametric Func]";
                case GeoNode::RenderType::Implicit:   return "[Implicit Func]";
                default:                              return "[Unknown]";
            }
        };

        for(size_t i = 0; i < wasm_function_ranges_buffer.size(); ++i) {
            auto& r = wasm_function_ranges_buffer[i];
            uint32_t node_id = draw_order[i]; // 获取对应的节点ID
            auto& node = graph.node_pool[node_id]; // 访问节点池获取类型信息

            std::cout << "Obj ID " << std::setw(2) << node_id
                      << " " << std::setw(18) << std::left << GetTypeStr(node.render_type)
                      << ": Start=" << std::setw(6) << r.start_index
                      << ", Count=" << std::setw(6) << r.point_count << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}