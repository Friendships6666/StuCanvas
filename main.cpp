// main.cpp
#include <iostream>
#include <vector>
#include "include/stucanvas/geometry/graph.hpp"
#include "include/stucanvas/types/point.hpp"
#include "include/stucanvas/types/mesh.hpp"
#include "include/stucanvas/reconstruction/quick_hull_3d.hpp"
#include "include/stucanvas/reconstruction/export/off.hpp"

int main() {
    using namespace StuCanvas;

    // 1. 创建图并设置世界空间和分辨率
    Graph<double> graph;
    graph.world_space_3d = {-4.0, -4.0, -4.0, 4.0, 4.0, 4.0}; // x_min, y_min, z_min, x_max, y_max, z_max
    graph.resolution_3d = {100, 100, 100};                       // 绘制分辨率（影响采样点密度）

    // 2. 创建立方体中心点
    uint64_t center_id = graph.CreateFreePoint_3D(0.0, 0.0, 0.0, "cube_center");

    // 3. 使用单节点柏拉图书创建正六面体（type=6，即立方体）
    uint64_t cube_id = graph.CreatePlatonicSolid_3D(center_id, 1.0, 6, "unit_cube");

    // 4. 运行图计算（求解器 + 绘制器），生成表面采样点云
    graph.Compute(); // 单线程即可

    // 5. 从立方体节点提取采样点云，用作 QuickHull 的输入
    Node<double>* cube_node = graph.GetNode(cube_id);
    const std::vector<Point3D<double>>& cube_surface_points = cube_node->result_points_3d;

    std::cout << "Number of surface points: " << cube_surface_points.size() << std::endl;

    if (cube_surface_points.size() < 4) {
        std::cerr << "Too few surface points to compute convex hull.\n";
        return 1;
    }

    // 6. 调用自实现的 QuickHull 3D（使用你修正后的版本）
    QuickHull3D<double> qhull;
    Mesh3D<double> convex_hull = qhull.Compute(cube_surface_points);

    std::cout << "Convex hull vertices: " << convex_hull.vertices.size()
              << ", faces: " << convex_hull.indices.size() / 3 << std::endl;

    // 7. 导出为 OFF 文件
    bool ok = Export::ToOFF("cube_hull.off", convex_hull);
    if (ok) {
        std::cout << "Successfully exported convex hull to cube_hull.off\n";
    } else {
        std::cerr << "Failed to export OFF file.\n";
        return 1;
    }

    return 0;
}