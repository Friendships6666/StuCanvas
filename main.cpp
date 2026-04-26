#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

// 1. 核心系统
#include "include/stucanvas/geometry/graph.hpp"
#include "include/stucanvas/geometry/solver.hpp"
#include "include/stucanvas/geometry/plotter.hpp"

// 2. 统计与重构器
#include "include/stucanvas/reconstruction/statistics.hpp"
#include "include/stucanvas/reconstruction/voxel_surface_reconstructor.hpp"

// 3. 导出工具
#include "include/stucanvas/reconstruction/export/off.hpp"

using namespace StuCanvas;
using namespace StuCanvas::Reconstruction;
using namespace std::chrono;

int main() {
    // --- 1. 环境初始化 ---
    Graph<double> graph;
    graph.world_space_3d = {-15.0, -15.0, -15.0, 15.0, 15.0, 15.0};

    // 对于曲面物体，建议将分辨率调高（如 400^3），
    // 这样“体素阶梯”会非常细微，视觉效果更接近平滑曲面。
    graph.resolution_3d = {400, 400, 400};

    std::cout << "===== StuCanvas: Curved Surface Reconstruction Test =====" << std::endl;

    // --- 2. 定义几何体依赖点 ---
    // 球体中心
    uint64_t p_sphere_c = graph.CreateFreePoint_3D(-8.0, 8.0, 0.0, "Sphere_Center");

    // 圆柱端点 (水平放置)
    uint64_t p_cyl_1 = graph.CreateFreePoint_3D(-5.0, -5.0, -5.0, "Cyl_Start");
    uint64_t p_cyl_2 = graph.CreateFreePoint_3D(5.0, -5.0, 5.0, "Cyl_End");

    // 圆锥点 (垂直向上)
    uint64_t p_cone_apex = graph.CreateFreePoint_3D(8.0, 8.0, 10.0, "Cone_Apex");
    uint64_t p_cone_base = graph.CreateFreePoint_3D(8.0, 8.0, 0.0, "Cone_Base");

    // --- 3. 创建几何物体 ---
    uint64_t sphere_id = graph.CreateSphere_3D(p_sphere_c, 5.0, "Target_Sphere");
    uint64_t cyl_id    = graph.CreateCylinder_3D(p_cyl_1, p_cyl_2, 3.0, "Target_Cylinder");
    uint64_t cone_id   = graph.CreateCone_3D(p_cone_apex, p_cone_base, 5.0, "Target_Cone");

    // --- 4. 执行 Plotting (生成点云壳) ---
    std::cout << "[Step 1] Plotting geometries..." << std::endl;
    auto t1 = high_resolution_clock::now();
    graph.Compute(std::thread::hardware_concurrency());
    auto t2 = high_resolution_clock::now();
    std::cout << " -> Time: " << duration_cast<milliseconds>(t2 - t1).count() << " ms" << std::endl;

    // --- 5. 循环处理每个物体并导出 ---
    struct Task { uint64_t id; std::string name; };
    std::vector<Task> tasks = {
        {sphere_id, "sphere"},
        {cyl_id,    "cylinder"},
        {cone_id,   "cone"}
    };

    for (const auto& task : tasks) {
        auto* node = graph.GetNode(task.id);
        const auto& points = node->result_points_3d;

        if (points.empty()) {
            std::cout << " [!] Skip " << task.name << " (Empty points)" << std::endl;
            continue;
        }

        std::cout << "\n[Reconstructing] " << node->name << " (" << points.size() << " points)" << std::endl;

        // 计算自适应步长
        double avg_d = CalculateAverageDistance(points);
        // 重构体素大小通常设为点云间距的 1.8 倍以保证奇偶填充闭合
        double recon_voxel_size = avg_d * 1.8;

        auto t_start = high_resolution_clock::now();

        // 执行体素表面重构 (Minecraft 风格但去除内面)
        Mesh3D<double> mesh = VoxelSurfaceReconstructor<double>::Reconstruct(points, recon_voxel_size);

        auto t_end = high_resolution_clock::now();

        std::cout << " -> Mesh Stats: Verts=" << mesh.vertices.size() << ", Tris=" << mesh.indices.size()/3 << std::endl;
        std::cout << " -> Recon Time: " << duration_cast<milliseconds>(t_end - t_start).count() << " ms" << std::endl;

        // 导出 OFF
        std::string filename = task.name + "_voxel_mesh.off";
        if (Export::ToOFF(filename, mesh)) {
            std::cout << " -> Saved to " << filename << std::endl;
        }
    }

    std::cout << "\n===== All Tasks Completed =====" << std::endl;
    return 0;
}