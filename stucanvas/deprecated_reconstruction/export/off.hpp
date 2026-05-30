#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>

// 假设此时你的上下文中已经能够找到 Mesh3D 的定义
// 如果需要，可以在这里 include 你的 mesh 头文件，例如:
#include "../../types/mesh.hpp"

namespace StuCanvas
{
    namespace Export 
    {
        /**
         * @brief 将 Mesh3D 导出为标准 .off 文件格式
         * 
         * @tparam T 数据类型 (float, double)
         * @param filepath 导出的文件路径 (例如 "output.off" 或 "/home/user/output.off")
         * @param mesh 要导出的 Mesh3D 对象
         * @return true 导出成功
         * @return false 导出失败（路径无效或无写入权限）
         */
        template <typename T>
        inline bool ToOFF(const std::filesystem::path& filepath, const Mesh3D<T>& mesh)
        {
            // 如果网格为空，可以选择依然导出空文件，或者直接拦截
            if (mesh.vertices.empty()) {
                std::cerr << "Warning: Exporting an empty mesh to " << filepath << std::endl;
            }

            // 打开文件输出流
            std::ofstream out(filepath, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "Error: Failed to open file for writing: " << filepath << std::endl;
                return false;
            }

            // 1. 写入 OFF 文件魔数头
            out << "OFF\n";

            // 2. 写入 顶点数、面片数、边数(边数通常填0即可)
            // 我们的 indices 是展开的，每 3 个 index 构成 1 个三角形面片
            size_t num_vertices = mesh.vertices.size();
            size_t num_faces = mesh.indices.size() / 3;
            out << num_vertices << " " << num_faces << " 0\n";

            // 3. 写入所有的顶点坐标
            // OFF 格式为: x y z
            // 使用较高的精度输出，防止浮点数据截断导致模型变形
            out.precision(8);
            out << std::fixed;
            for (const auto& v : mesh.vertices) {
                out << v.position.x << " " 
                    << v.position.y << " " 
                    << v.position.z << "\n";
            }

            // 4. 写入所有的面片信息
            // OFF 三角面片格式为: 3 index1 index2 index3
            for (size_t i = 0; i < mesh.indices.size(); i += 3) {
                out << "3 " << mesh.indices[i] << " " 
                            << mesh.indices[i+1] << " " 
                            << mesh.indices[i+2] << "\n";
            }

            // 5. 确保写入完成并关闭
            out.flush();
            out.close();

            std::cout << "Successfully exported mesh to " << filepath << " (" 
                      << num_vertices << " vertices, " << num_faces << " faces)" << std::endl;
            return true;
        }
    } // namespace Export
} // namespace StuGeometry