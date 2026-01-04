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
void ExportPoints(const std::string& filename, const AlignedVector<PointData>& buffer) {
    std::ofstream outfile(filename);
    outfile << "# [X_Clip] [Y_Clip] [Func_ID]\n" << std::fixed << std::setprecision(6);
    for (const auto& pt : buffer) {
        outfile << pt.position.x << " " << pt.position.y << " " << pt.function_index << "\n";
    }
    outfile.close();
}

int main() {
    try {
        // =========================================================
        // 1. 【严格执行】用户指定的初始视图参数
        // =========================================================
        double screen_width = 2560.0;
        double screen_height = 1600.0;
        double offset_x = 0.0;
        double offset_y = 0.0;
        double zoom = 0.1;

        double aspect_ratio = screen_width / screen_height;
        double wppx = (2.0 * aspect_ratio) / (zoom * screen_width);
        double wppy = -2.0 / (zoom * screen_height);

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

        // 2. 构建“套娃”依赖场景
        // A. 基础圆 circumCircle (Rank 2)
        uint32_t A = CreatePoint(graph, {-5.0}, {0.0});
        uint32_t B = CreatePoint(graph, {5.0}, {0.0});
        uint32_t C = CreatePoint(graph, {0.0}, {5.0});
        uint32_t circumCircle = CreateCircleThreePoints(graph, A, B, C);

        // B. 图解附着点 P_cp (Rank 3)
        // 初始猜测在 (0, 5) 附近，即 C 点位置
        uint32_t P_cp = CreateAnalyticalConstrainedPoint(graph, circumCircle, {0.0}, {5.1});

        // C. 附着点上的圆 circle_on_cp (Rank 4)
        // 圆心是 P_cp，半径为 2.0
        uint32_t circle_on_cp = CreateCircle(graph, P_cp, {2.0});

        std::vector<uint32_t> g_draw_order = { circumCircle, P_cp, circle_on_cp };

        // =========================================================
        // Step 1: 初始化
        // =========================================================
        std::cout << "[Step 1] Initial Full Render..." << std::endl;

        commit_incremental_updates(graph, view, g_draw_order);

        auto& data_cp = std::get<Data_AnalyticalConstrainedPoint>(graph.node_pool[P_cp].data);
        std::cout << "Initial P_cp World Pos: (" << data_cp.x << ", " << data_cp.y << ")" << std::endl;
        ExportPoints("step1.txt", wasm_final_contiguous_buffer);

        // =========================================================
        // Step 2: 移动 A 点（触发连锁反应）
        // =========================================================
        std::cout << "\n[Step 2] Moving Point A to (-10.0, 0.0)..." << std::endl;
        UpdateFreePoint(graph, A, {-10.0}, {0.0});

        // 这次调用会完成：A 变 -> circumCircle 变 -> P_cp 重寻址 -> circle_on_cp 随动
        commit_incremental_updates(graph, view, g_draw_order);

        std::cout << "Updated P_cp World Pos: (" << data_cp.x << ", " << data_cp.y << ")" << std::endl;


        ExportPoints("step2.txt", wasm_final_contiguous_buffer);

        // =========================================================
        // Step 3: 视图缩放（测试图解重准度）
        // =========================================================
        std::cout << "\n[Step 3] Zooming In (x2)..." << std::endl;
        view.zoom *= 2;


        // 视图更新模式：不解方程，只重采样
        commit_viewport_update(graph, view, g_draw_order);

        std::cout << "Final P_cp World Pos (After Zoom): (" << data_cp.x << ", " << data_cp.y << ")" << std::endl;
        ExportPoints("step3.txt", wasm_final_contiguous_buffer);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}