#pragma once

#include <vector>
#include <cmath>
#include <functional>
#include "../../types/point.hpp"
#include "../../types/mesh.hpp"

namespace StuCanvas::Reconstruction::Field {

template <typename T>
struct ParametricMesh3DDescriptor {
    using Scalar = T;

    // 三个坐标分量函数，输入(u,v)，输出对应坐标值
    std::function<T(T, T)> x_func;
    std::function<T(T, T)> y_func;
    std::function<T(T, T)> z_func;

    T u_min = 0, u_max = 1;       // u 参数范围
    T v_min = 0, v_max = 1;       // v 参数范围
    T u_step = T(0.1);            // u 方向采样步长
    T v_step = T(0.1);            // v 方向采样步长

    // 可选的世界空间裁剪范围（默认不裁剪，即包含所有生成的点）
    bool  enable_clip = false;
    T x_min = 0, x_max = 0;
    T y_min = 0, y_max = 0;
    T z_min = 0, z_max = 0;
};

template <typename T>
class ParametricMesh3D {
public:
    static Mesh3D<T> Generate(const ParametricMesh3DDescriptor<T>& desc) {
        Mesh3D<T> mesh;
        if (desc.u_step <= T(0) || desc.v_step <= T(0)) return mesh;

        // 计算两个方向的采样点数
        size_t nu = static_cast<size_t>(std::floor((desc.u_max - desc.u_min) / desc.u_step)) + 1;
        size_t nv = static_cast<size_t>(std::floor((desc.v_max - desc.v_min) / desc.v_step)) + 1;
        if (nu < 2 || nv < 2) return mesh;

        // 1. 逐点采样并存储顶点（可选裁剪）
        std::vector<Point3D<T>> vertices;
        std::vector<bool> valid(nu * nv, false);      // 记录点是否有效（用于裁剪）
        vertices.reserve(nu * nv);

        for (size_t i = 0; i < nu; ++i) {
            T u = desc.u_min + i * desc.u_step;
            for (size_t j = 0; j < nv; ++j) {
                T v = desc.v_min + j * desc.v_step;
                T x = desc.x_func(u, v);
                T y = desc.y_func(u, v);
                T z = desc.z_func(u, v);
                vertices.emplace_back(x, y, z);

                // 简单裁剪判断
                bool inside = true;
                if (desc.enable_clip) {
                    if (x < desc.x_min || x > desc.x_max ||
                        y < desc.y_min || y > desc.y_max ||
                        z < desc.z_min || z > desc.z_max)
                        inside = false;
                }
                valid[i * nv + j] = inside;
            }
        }

        // 2. 建立有效顶点到输出网格索引的映射（剔除裁剪掉的点）
        std::vector<int> newIndex(vertices.size(), -1);
        size_t vertexCount = 0;
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (valid[i]) {
                mesh.vertices.push_back({vertices[i]});
                newIndex[i] = static_cast<int>(vertexCount++);
            }
        }

        // 3. 生成三角形 (每个四边形分为两个三角形)
        for (size_t i = 0; i < nu - 1; ++i) {
            for (size_t j = 0; j < nv - 1; ++j) {
                // 四边形四个角在顶点数组中的线性索引
                int idx00 = newIndex[i * nv + j];
                int idx10 = newIndex[(i + 1) * nv + j];
                int idx01 = newIndex[i * nv + (j + 1)];
                int idx11 = newIndex[(i + 1) * nv + (j + 1)];

                // 仅当四边形的所有四个顶点都有效时才生成两个三角形
                if (idx00 < 0 || idx10 < 0 || idx01 < 0 || idx11 < 0)
                    continue;

                // 三角形 1 (idx00, idx10, idx11)
                mesh.indices.push_back(static_cast<uint32_t>(idx00));
                mesh.indices.push_back(static_cast<uint32_t>(idx10));
                mesh.indices.push_back(static_cast<uint32_t>(idx11));

                // 三角形 2 (idx00, idx11, idx01)
                mesh.indices.push_back(static_cast<uint32_t>(idx00));
                mesh.indices.push_back(static_cast<uint32_t>(idx11));
                mesh.indices.push_back(static_cast<uint32_t>(idx01));
            }
        }

        return mesh;
    }
};

// 便捷函数接口
template <typename T>
Mesh3D<T> plot_parametric_3d_mesh(const ParametricMesh3DDescriptor<T>& desc) {
    return ParametricMesh3D<T>::Generate(desc);
}

} // namespace StuCanvas::Reconstruction::Field