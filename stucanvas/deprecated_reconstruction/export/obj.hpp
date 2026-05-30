#pragma once

#include <fstream>
#include <iostream>
#include <filesystem>

#include "../../types/mesh.hpp"

namespace StuCanvas::Export {

    /**
     * @brief 将 Mesh3D 导出为最简 OBJ 格式（仅顶点和三角面）
     * @tparam T 坐标类型（float / double）
     * @param filepath 输出文件路径（例如 "output.obj"）
     * @param mesh 要导出的网格
     * @return 成功返回 true，失败返回 false
     */
    template <typename T>
    inline bool ToOBJ(const std::filesystem::path& filepath, const Mesh3D<T>& mesh)
    {
        std::ofstream out(filepath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "Error: Cannot write to " << filepath << std::endl;
            return false;
        }

        out.precision(8);
        out << std::fixed;

        // 顶点
        for (const auto& v : mesh.vertices) {
            out << "v " << v.position.x << " "
                << v.position.y << " "
                << v.position.z << "\n";
        }

        // 三角形面（OBJ 索引从 1 开始）
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            out << "f " << mesh.indices[i] + 1 << " "
                << mesh.indices[i + 1] + 1 << " "
                << mesh.indices[i + 2] + 1 << "\n";
        }

        out.close();
        std::cout << "Exported " << mesh.vertices.size() << " vertices, "
                  << mesh.indices.size() / 3 << " faces to " << filepath << std::endl;
        return true;
    }

} // namespace StuCanvas::Export