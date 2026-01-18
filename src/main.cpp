#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <chrono>

#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/graph/GeoCommands.h"
#include "../include/plot/plotCall.h"



// =========================================================
// 💡 辅助函数：导出当前显存数据为文本
// =========================================================
void ExportPoints(int index) {
    std::string filename = "points" + std::to_string(index) + ".txt";
    std::ofstream outFile(filename);
    if (!outFile.is_open()) return;

    outFile << std::fixed << std::setprecision(6);
    for (const auto& pt : wasm_final_contiguous_buffer) {
        // 格式: x_clip y_clip function_id
        outFile << pt.position.x << " "
                << pt.position.y << " "
                << pt.function_index << "\n";
    }
    outFile.close();
    std::cout << "[Disk] Exported " << wasm_final_contiguous_buffer.size()
              << " points to " << filename << std::endl;
}

int main() {
    try {
        std::cout << "=== GeoEngine: 1D Command Bus & Auto-Pipeline Test ===" << std::endl;

        GeometryGraph graph;
        std::vector<GeoCommand::CommandPacket> bus;

        // =========================================================
        // STAGE 1: 初始化视口并创建基础几何体
        // =========================================================
        std::cout << "\n[Stage 1] Initializing entities..." << std::endl;

        // 1. 设置屏幕大小 (2560x1600)
        GeoCommand::CommandPacket pkgSize(GeoCommand::OpCode::UPDATE_VIEW_SIZE);
        pkgSize.d0 = 2560.0; pkgSize.d1 = 1600.0;
        bus.push_back(pkgSize);

        // 2. 创建点 A(-5, 0) -> 产生 ID 1,2(标量), 3(点)
        GeoCommand::CommandPacket pkgA(GeoCommand::OpCode::CREATE_FREE_POINT);
        pkgA.s0 = "-5.0"; pkgA.s1 = "0.0";
        bus.push_back(pkgA);

        // 3. 创建点 B(5, 0) -> 产生 ID 4,5(标量), 6(点)
        GeoCommand::CommandPacket pkgB(GeoCommand::OpCode::CREATE_FREE_POINT);
        pkgB.s0 = "5.0"; pkgB.s1 = "0.0";
        bus.push_back(pkgB);

        // 4. 创建线段 L (依赖 ID 3 和 6)
        GeoCommand::CommandPacket pkgL(GeoCommand::OpCode::CREATE_SEGMENT_2P);
        pkgL.id0 = 3; pkgL.id1 = 6;
        bus.push_back(pkgL);

        // 执行第一波总线指令
        GeoCommand::Execute(graph, bus);
        ExportPoints(1);

        // =========================================================
        // STAGE 2: 移动点 A -> (-10, 5)
        // =========================================================
        std::cout << "\n[Stage 2] Moving Point A to (-10, 5)..." << std::endl;


        GeoCommand::CommandPacket moveA(GeoCommand::OpCode::UPDATE_POINT_SCALAR);
        moveA.id0 = 3;      // 目标点 A
        moveA.s0 = "sin(-10)"; // 新 X 公式
        moveA.s1 = "5.0";   // 新 Y 公式
        bus.push_back(moveA);

        // 执行并自动触发增量渲染
        GeoCommand::Execute(graph, bus);
        ExportPoints(2);

        // =========================================================
        // STAGE 3: 平移视图 (Camera Pan)
        // =========================================================
        std::cout << "\n[Stage 3] Panning Viewport (Offset +100, +50)..." << std::endl;


        GeoCommand::CommandPacket panView(GeoCommand::OpCode::UPDATE_VIEW_TRANSFORM);
        panView.d0 = 3; // offset_x
        panView.d1 = 3;  // offset_y
        panView.d2 = 0.1;   // 维持 zoom
        bus.push_back(panView);

        // 执行并自动触发全量重投影 (Viewport 模式)
        GeoCommand::Execute(graph, bus);
        ExportPoints(3);

        // =========================================================
        // 最终验证
        // =========================================================
        auto& resA = graph.get_node_by_id(3).result;
        std::cout << "\n[Final Verification]" << std::endl;
        std::cout << "Point A World Pos: (" << resA.x << ", " << resA.y << ")" << std::endl;
        std::cout << "View Offset:      (" << graph.view.offset_x << ", " << graph.view.offset_y << ")" << std::endl;
        std::cout << "Total Objects:    " << graph.node_pool.size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}