#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>

// 包含项目核心头文件
#include "include/stucanvas/geometry/graph.hpp"
#include "include/stucanvas/geometry/solver.hpp"
#include "include/stucanvas/geometry/plotter.hpp"
#include "include/stucanvas/reconstruction/statistics.hpp"
#include "include/stucanvas/reconstruction/voxel_boolean_3d.hpp"

using namespace StuCanvas;
using namespace std::chrono;

int main() {
    // 1. 初始化 Graph 环境
    Graph<double> graph;
    graph.world_space_3d = {-10.0, -10.0, -10.0, 10.0, 10.0, 10.0}; // 世界范围
    graph.resolution_3d = {200, 200, 200}; // 设置分辨率 (200^3 体素采样)

    // 2. 创建依赖点
    uint64_t p_origin = graph.CreateFreePoint_3D(0, 0, 0, "Center");
    uint64_t p_top    = graph.CreateFreePoint_3D(0, 0, 8, "Apex/Top");
    uint64_t p_norm   = graph.CreateFreePoint_3D(0, 1, 0, "Normal_Ref");

    // 3. 创建几何物体
    // 圆锥面 (顶点, 底面中心, 半径)
    uint64_t cone_id = graph.CreateCone_3D(p_top, p_origin, 4.0, "Surface_Cone");

    // 圆柱面 (底面, 顶面, 半径)
    uint64_t cyl_id  = graph.CreateCylinder_3D(p_origin, p_top, 2.5, "Surface_Cylinder");

    // 3D 空间圆 (中心, 法向参考点, 半径)
    uint64_t circ_id = graph.CreateCircle_3D(p_origin, p_norm, 5.0, "Curve_Circle");

    // 4. 执行并行计算并计时
    std::cout << "[Step 1] Starting Graph Computation..." << std::endl;
    auto start_time = high_resolution_clock::now();

    // 使用所有核心进行并行计算
    graph.Compute(std::thread::hardware_concurrency());

    auto end_time = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end_time - start_time);
    std::cout << "Computation finished in: " << duration.count() << " ms" << std::endl;

    // 5. 统计与点云输出
    std::ofstream outFile("points.txt");
    if (!outFile) return -1;
    outFile << std::fixed << std::setprecision(6);

    uint64_t target_nodes[] = {cone_id, cyl_id, circ_id};
    for (uint64_t id : target_nodes) {
        auto* node = graph.GetNode(id);
        const auto& cloud = node->result_points_3d;

        // 计算平均间距 (利用 statistics.hpp)
        double avg_dist = CalculateAverageDistance(cloud);

        std::cout << "Node [" << node->name << "]: "
                  << cloud.size() << " points, Avg Spacing: " << avg_dist << std::endl;

        // 输出到文件
        outFile << "# Node: " << node->name << "\n";
        for (const auto& p : cloud) {
            outFile << p.x << " " << p.y << " " << p.z << "\n";
        }
    }

    // 6. 执行布尔运算 (Reconstruction 系统)
    // 示例：从圆柱中挖掉圆锥的部分 (Difference)
    std::cout << "[Step 2] Performing Voxel Boolean (Difference)..." << std::endl;

    auto bool_start = high_resolution_clock::now();

    const auto& cone_cloud = graph.GetNode(cone_id)->result_points_3d;
    const auto& cyl_cloud  = graph.GetNode(cyl_id)->result_points_3d;

    // 体素大小建议设为世界跨度/分辨率 (例如 20/200 = 0.1)
    double voxel_size = 0.1;

    // 执行：圆柱 - 圆锥
    auto result_cloud = Reconstruction::VoxelBoolean3D<double>::Difference(
        cyl_cloud, cone_cloud, voxel_size
    );

    auto bool_end = high_resolution_clock::now();
    std::cout << "Boolean operation finished in: "
              << duration_cast<milliseconds>(bool_end - bool_start).count() << " ms" << std::endl;
    std::cout << "Result Cloud Size: " << result_cloud.size() << " points." << std::endl;

    // 7. 保存布尔运算结果
    outFile << "# Boolean Result (Cylinder - Cone)\n";
    for (const auto& p : result_cloud) {
        outFile << p.x << " " << p.y << " " << p.z << "\n";
    }

    outFile.close();
    std::cout << "All points saved to points.txt" << std::endl;

    return 0;
}