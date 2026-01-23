#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include "../include/graph/GeoGraph.h"
#include "../include/graph/GeoFactory.h"
#include "../include/graph/GeoCommands.h"
#include "../include/plot/plotCall.h"

/**
 * @brief 辅助函数：导出当前渲染缓冲区中的所有点
 * 格式：x y function_index
 */
void ExportPoints(const GeometryGraph& graph, int file_index) {
    std::string filename = "points_" + std::to_string(file_index) + ".txt";
    std::ofstream outFile(filename);

    if (!outFile.is_open()) {
        std::cerr << "无法创建文件: " << filename << std::endl;
        return;
    }

    // 设置高精度输出
    outFile << std::fixed << std::setprecision(6);

    for (const auto& pt : graph.final_points_buffer) {
        // 这里的 pt.position.x/y 通常是 NDC 坐标 [-1, 1]
        outFile << pt.position.x << " "
                << pt.position.y << " "
                << pt.function_index << "\n";
    }

    outFile.close();
    std::cout << "[Disk] 已保存 " << graph.final_points_buffer.size()
              << " 个点到 " << filename << std::endl;
}

// 高级 Universe 检查器
void InspectUniverse(const GeometryGraph& graph, const std::string& title) {
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "  宇宙状态: " << title << " | HEAD: v" << graph.head_version_id << std::endl;
    std::cout << "  相机位置: (" << std::fixed << std::setprecision(1) << graph.view.offset_x << ", " << graph.view.offset_y << ")" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    std::cout << "  ID | 名字   | 类型 | 状态  | 采样点数 | 世界坐标 (数学)        | 相对坐标 (渲染用)" << std::endl;

    for (const auto& node : graph.node_pool) {
        if (!node.active) continue;

        std::string type_name = "OTHER";
        if (node.type == GeoType::POINT_FREE) type_name = "FREE_P";
        else if (node.type == GeoType::POINT_MID)  type_name = "MID_P ";
        else if (node.type == GeoType::LINE_SEGMENT) type_name = "LINE  ";
        else if (node.type == GeoType::POINT_CONSTRAINED) type_name = "CONSTP";

        std::cout << "  " << std::setw(2) << node.id << " | "
                  << std::setw(6) << node.config.name << " | "
                  << type_name << " | "
                  << (GeoStatus::ok(node.status) ? "VALID" : "ERROR") << " | "
                  << std::setw(8) << node.current_point_count << " | ";

        if (GeoType::is_point(node.type)) {
            std::cout << "(" << std::setw(10) << node.result.x << ", " << std::setw(10) << node.result.y << ") | "
                      << "(" << std::setw(5) << node.result.x_view << ", " << std::setw(5) << node.result.y_view << ")";
        } else if (node.type == GeoType::LINE_SEGMENT) {
            std::cout << "连结 ID:" << node.parents[0] << " 和 ID:" << node.parents[1];
        }
        std::cout << std::endl;
    }
    std::cout << std::string(80, '=') << std::endl;
}

int main() {
    try {
        std::cout << "=== GeoEngine: Full Precision & Plot Export Test ===" << std::endl;

        GeometryGraph graph;
        std::vector<GeoCommand::CommandPacket> bus;
        int execute_counter = 0; // 用于记录 execute 次数

        // ---------------------------------------------------------
        // 1. 初始化视口
        // ---------------------------------------------------------
        GeoCommand::CommandPacket pkgView(GeoCommand::OpCode::UPDATE_VIEW_TRANSFORM);
        pkgView.d0 = 100000.0; pkgView.d1 = 100000.0; pkgView.d2 = 0.1;
        bus.push_back(pkgView);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_0.txt

        // ---------------------------------------------------------
        // 2. V0: 创建点 A 和 B
        // ---------------------------------------------------------
        std::cout << "\n[Action] 创建点 A 和 B..." << std::endl;
        GeoCommand::CommandPacket pA(GeoCommand::OpCode::CREATE_FREE_POINT);
        pA.s0 = "100000"; pA.s1 = "100000"; pA.name = "A"; bus.push_back(pA);

        GeoCommand::CommandPacket pB(GeoCommand::OpCode::CREATE_FREE_POINT);
        pB.s0 = "100001"; pB.s1 = "100001"; pB.name = "B"; bus.push_back(pB);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_1.txt
        InspectUniverse(graph, "V0: 点已创建");

        // ---------------------------------------------------------
        // 3. V1: 创建线段 L
        // ---------------------------------------------------------
        std::cout << "\n[Action] 创建线段 L..." << std::endl;
        GeoCommand::CommandPacket pL(GeoCommand::OpCode::CREATE_SEGMENT_2P);
        pL.id0 = 1; pL.id1 = 2; pL.name = "L"; bus.push_back(pL);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_2.txt
        InspectUniverse(graph, "V1: 线段 L 已创建");

        // ---------------------------------------------------------
        // 4. V2: 创建中点 M
        // ---------------------------------------------------------
        std::cout << "\n[Action] 创建中点 M..." << std::endl;
        GeoCommand::CommandPacket pM(GeoCommand::OpCode::CREATE_MID_POINT);
        pM.id0 = 1; pM.id1 = 2; pM.name = "M"; bus.push_back(pM);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_3.txt
        InspectUniverse(graph, "V2: 中点 M 已计算");

        // ---------------------------------------------------------
        // 5. Undo 回到 V1 (中点消失)
        // ---------------------------------------------------------
        std::cout << "\n[Action] Undo 回到 V1..." << std::endl;
        GeoCommand::Undo(graph);
        ExportPoints(graph, execute_counter++); // points_4.txt (撤销后的状态)
        InspectUniverse(graph, "Undo 结果");

        // ---------------------------------------------------------
        // 6. V3: 分支 - 创建约束点 P
        // ---------------------------------------------------------
        std::cout << "\n[Action] Branching: 在线段 L 上创建约束点 P..." << std::endl;
        GeoCommand::CommandPacket pP(GeoCommand::OpCode::CREATE_CONSTRAINED_POINT);
        pP.id0 = 3; // 目标线段 L
        pP.s0 = "100002-3"; pP.s1 = "100000.4-1";
        pP.name = "P"; bus.push_back(pP);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_5.txt
        InspectUniverse(graph, "V3: 约束点 P 已吸附");

        // ---------------------------------------------------------
        // 7. Checkout 跳转回 V2
        // ---------------------------------------------------------
        std::cout << "\n[Action] Checkout 跳转回 V2..." << std::endl;
        GeoCommand::CheckoutVersion(graph, 2);
        ExportPoints(graph, execute_counter++); // points_6.txt
        InspectUniverse(graph, "Checkout V2 结果");

        // ---------------------------------------------------------
        // 8. 删除测试：删除点 A (ID 1)
        // ---------------------------------------------------------
        std::cout << "\n[Action] 删除点 A (ID 1)..." << std::endl;
        GeoCommand::CommandPacket delA(GeoCommand::OpCode::DELETE_PHYSICAL);
        delA.id0 = 1; bus.push_back(delA);

        GeoCommand::Execute(graph, bus);
        ExportPoints(graph, execute_counter++); // points_7.txt
        InspectUniverse(graph, "V4: 删除 A 后的级联结果");

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}