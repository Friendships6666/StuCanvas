#include <iostream>
#include <vector>
#include <chrono>

// 1. 引入 StuCanvas 核心
#include "include/stucanvas/geometry/graph.hpp"

// 2. 引入封装模块
#include "include/stucanvas/reconstruction/qhull_wrapper.hpp"
#include "include/stucanvas/reconstruction/boolean_engine.hpp"
#include "include/stucanvas/reconstruction/export/off.hpp"

using namespace StuCanvas;
using namespace StuCanvas::Reconstruction;

int main() {
    // --- 第一步：初始化 Graph 与 采样精度 ---
    Graph<double> graph;
    graph.resolution_3d = {120.0, 120.0, 120.0};
    graph.world_space_3d = {-15.0, -15.0, -15.0, 15.0, 15.0, 15.0};

    // --- 第二步：构造几何体依赖图 ---

    // 1. 大圆柱 (母体): 竖直 (沿 Y 轴), 半径 4.0, 长度 12.0
    uint64_t p1 = graph.CreateFreePoint_3D(0.0, -6.0, 0.0, "Base_Bottom");
    uint64_t p2 = graph.CreateFreePoint_3D(0.0,  6.0, 0.0, "Base_Top");
    uint64_t big_id = graph.CreateCylinder_3D(p1, p2, 4.0, "BigCylinder");

    // 2. 横向圆柱 (刀具1): 水平贯穿 (沿 X 轴), 半径 2.0
    uint64_t p3 = graph.CreateFreePoint_3D(-8.0, 0.0, 0.0, "Horiz_Left");
    uint64_t p4 = graph.CreateFreePoint_3D( 8.0, 0.0, 0.0, "Horiz_Right");
    uint64_t horiz_id = graph.CreateCylinder_3D(p3, p4, 2.0, "HorizontalCutter");

    // 3. 纵向小圆柱 (刀具2): 竖直贯穿 (沿 Y 轴), 半径 1.5
    // 长度设为 14.0 (比母体长)，确保完全打通顶面和底面
    uint64_t p5 = graph.CreateFreePoint_3D(0.0, -7.0, 0.0, "Vert_Bottom");
    uint64_t p6 = graph.CreateFreePoint_3D(0.0,  7.0, 0.0, "Vert_Top");
    uint64_t vert_id = graph.CreateCylinder_3D(p5, p6, 1.5, "VerticalCutter");

    std::cout << "[Step 1] 正在生成三组点云..." << std::endl;
    graph.Compute();

    // --- 第三步：使用 QhullWrapper 进行网格重构 ---
    std::cout << "[Step 2] 正在重构三个原始网格..." << std::endl;
    Mesh3D<double> meshBig   = QhullWrapper<double>::Compute(graph.GetNode(big_id)->result_points_3d);
    Mesh3D<double> meshHoriz = QhullWrapper<double>::Compute(graph.GetNode(horiz_id)->result_points_3d);
    Mesh3D<double> meshVert  = QhullWrapper<double>::Compute(graph.GetNode(vert_id)->result_points_3d);

    // --- 第四步：执行两次连续布尔运算 ---
    std::cout << "[Step 3] 正在执行多重布尔运算..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    // 第一次：大圆柱 - 横向孔
    Mesh3D<double> intermediate = BooleanEngine<double>::Difference(meshBig, meshHoriz);

    // 第二次：中间结果 - 纵向孔
    Mesh3D<double> finalMesh = BooleanEngine<double>::Difference(intermediate, meshVert);

    auto end = std::chrono::high_resolution_clock::now();

    // --- 第五步：结果验证与导出 ---
    if (finalMesh.indices.empty()) {
        std::cerr << "[Error] 布尔运算流程中断，请检查输入流形。" << std::endl;
        return -1;
    }

    std::chrono::duration<double, std::milli> dur = end - start;
    std::cout << "\n========================================" << std::endl;
    std::cout << "       双重挖孔布尔运算报告             " << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << " 最终顶点数: " << finalMesh.vertices.size() << std::endl;
    std::cout << " 最终面片数: " << finalMesh.indices.size() / 3 << std::endl;
    std::cout << " 两次布尔总耗时: " << dur.count() << " ms" << std::endl;
    std::cout << "========================================\n" << std::endl;

    Export::ToOFF("double_drilled_cylinder.off", finalMesh);
    std::cout << "[Success] 结果已导出至 double_drilled_cylinder.off" << std::endl;

    return 0;
}