	#include <iostream>
	#include <fstream>
	#include <chrono>
	#include <string>
	#include <vector>
	#include "include/stucanvas/geometry/graph.hpp"
	using namespace StuCanvas;
	using namespace std::chrono;
	/**
	 * @brief 将多个 Mesh3D 合并为一个整体的 Mesh3D
	 * @param meshes 待合并的网格集合
	 * @return 合并后的整体网格
	 */
	Mesh3D<double> MergeMeshes(const std::vector<Mesh3D<double>>& meshes)
	{
	    Mesh3D<double> merged_mesh;
	    uint32_t vertex_offset = 0;
	    for (const auto& mesh : meshes)
	    {
	        // 1. 追加顶点
	        merged_mesh.vertices.insert(merged_mesh.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
	        // 2. 追加索引 (需要加上顶点偏移量)
	        for (uint32_t idx : mesh.indices)
	        {
	            merged_mesh.indices.push_back(idx + vertex_offset);
	        }
	        // 3. 更新偏移量
	        vertex_offset += static_cast<uint32_t>(mesh.vertices.size());
	    }
	    return merged_mesh;
	}
	/**
	 * @brief 将 Mesh3D 导出为 OFF 格式文件
	 * @param filename 输出文件名
	 * @param mesh 待导出的网格
	 */
	void ExportMeshToOFF(const std::string& filename, const Mesh3D<double>& mesh)
	{
	    std::ofstream ofs(filename);
	    if (!ofs.is_open())
	    {
	        std::cerr << "Failed to open file: " << filename << std::endl;
	        return;
	    }
	    // OFF 文件头
	    ofs << "OFF\n";
	    size_t num_vertices = mesh.vertices.size();
	    size_t num_faces = mesh.indices.size() / 3;
	    ofs << num_vertices << " " << num_faces << " 0\n";
	    // 写入顶点
	    for (const auto& vert : mesh.vertices)
	    {
	        ofs << vert.position.x << " " << vert.position.y << " " << vert.position.z << "\n";
	    }
	    // 写入面 (三角面)
	    for (size_t i = 0; i < mesh.indices.size(); i += 3)
	    {
	        ofs << "3 " << mesh.indices[i] << " " << mesh.indices[i + 1] << " " << mesh.indices[i + 2] << "\n";
	    }
	    ofs.close();
	}
	int main()
	{
	    std::cout << "\n====================================================" << std::endl;
	    std::cout << "Starting Geometric Convex Decomposition Tests" << std::endl;
	    std::cout << "====================================================\n" << std::endl;
	    auto run_test = [](const std::string& name, int platonic_type, double radius, double acd_ratio, size_t max_parts) {
	        std::cout << "====================================================" << std::endl;
	        std::cout << "Processing: " << name << std::endl;
	        Graph<double> graph;
	        graph.world_space_3d = {-10.0, -10.0, -10.0, 10.0, 10.0, 10.0};
	        graph.resolution_3d = {200.0, 200.0, 200.0};
	        uint64_t center_id = graph.CreateFreePoint_3D(0.0, 0.0, 0.0, "Center");
	        auto t_start = high_resolution_clock::now();
	        uint64_t geo_id = 0;
	        if (platonic_type == -1) { // -1 表示球体
	            geo_id = graph.CreateSphere_3D(center_id, radius, name);
	        } else {
	            geo_id = graph.CreatePlatonicSolid_3D(center_id, radius, platonic_type, name);
	        }
	        graph.Compute();
	        auto t_plot_end = high_resolution_clock::now();
	        Node<double>* node = graph.GetNode(geo_id);
	        size_t point_count = node->result_points_3d.size();
	        std::cout << "  [Info] Generated points count: " << point_count << std::endl;
	        std::cout << "  [Time] Plotting time: "
	                  << duration_cast<milliseconds>(t_plot_end - t_start).count() << " ms" << std::endl;
	        auto t_acd_start = high_resolution_clock::now();
	        node->ComputeACDMesh3D(acd_ratio, max_parts);
	        auto t_acd_end = high_resolution_clock::now();
	        size_t part_count = node->result_meshes_3d.size();
	        std::cout << "  [Info] Convex decomposition parts: " << part_count << std::endl;
	        std::cout << "  [Time] ACD time: "
	                  << duration_cast<milliseconds>(t_acd_end - t_acd_start).count() << " ms" << std::endl;
	        // 合并所有分解后的子网格为一个整体
	        Mesh3D<double> merged_hull = MergeMeshes(node->result_meshes_3d);
	        std::cout << "  [Info] Merged Hull -> Vertices: " << merged_hull.vertices.size()
	                  << " | Faces: " << merged_hull.indices.size() / 3 << std::endl;
	        // 导出整体的 OFF 文件
	        std::string filename = name + "_ACD.off";
	        ExportMeshToOFF(filename, merged_hull);
	        std::cout << "  [Export] " << filename << " generated." << std::endl;
	    };
	    // 测试球体
	    run_test("Sphere_R3", -1, 3.0, 0.02, 8);
	    // 测试正四面体 (4)
	    run_test("Tetrahedron", 4, 3.0, 0.05, 8);
	    // 测试正六面体 (6)
	    run_test("Hexahedron", 6, 3.0, 0.05, 8);
	    // 测试正八面体 (8)
	    run_test("Octahedron", 8, 3.0, 0.05, 8);
	    // 测试正十二面体 (12)
	    run_test("Dodecahedron", 12, 3.0, 0.05, 16);
	    // 测试正二十面体 (20)
	    run_test("Icosahedron", 20, 3.0, 0.05, 16);
	    std::cout << "\n====================================================" << std::endl;
	    std::cout << "All tests completed. OFF files generated." << std::endl;
	    std::cout << "====================================================" << std::endl;
	    return 0;
	}