/***************************************************************************
* Copyright (c) 2026 StuCanvas Team                                        *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
***************************************************************************/

#pragma once

#include <vector>
#include <unordered_map>
#include <Eigen/Dense>
#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include "../types/point.hpp"
#include "../types/mesh.hpp"

namespace StuCanvas::Reconstruction
{
    /**
     * @brief 基于 Eigen 和 Qhull 的工业级网格重构包装器
     *
     * 该类负责将离散的点云转换为符合 Manifold 库严格拓扑要求的 Mesh3D 结构。
     * 使用 Eigen 进行几何校准，确保三角形环绕顺序（Winding Order）绝对正确。
     */
    template <typename T = double>
    class QhullWrapper
    {
    public:
        /**
         * @brief 将点云重构为拓扑完美的凸包网格
         * @param cloud 输入的 Point3D 向量
         * @return Mesh3D<T> 包含去重顶点和校正后索引的网格
         */
        static Mesh3D<T> Compute(const std::vector<Point3D<T>>& cloud)
        {
            Mesh3D<T> resultMesh;
            if (cloud.size() < 4) return resultMesh;

            // 1. 转换数据为 Qhull 要求的 double 展平数组
            std::vector<double> flat_points;
            flat_points.reserve(cloud.size() * 3);
            for (const auto& p : cloud) {
                flat_points.push_back(static_cast<double>(p.x));
                flat_points.push_back(static_cast<double>(p.y));
                flat_points.push_back(static_cast<double>(p.z));
            }

            // 2. 运行 Qhull
            orgQhull::Qhull qh;
            try {
                // "Qt": 强制三角化 (Manifold 必须)
                // "Pp": 抑制非致命精度报警
                qh.runQhull("", 3, static_cast<int>(cloud.size()), flat_points.data(), "Qt Pp");
            } catch (...) {
                return resultMesh;
            }

            // 3. 建立顶点映射与去重逻辑
            // 使用 Qhull 顶点的原始 ID 作为 Key，确保物理上的“顶点共享”
            std::unordered_map<int, uint32_t> qh_id_to_mesh_idx;
            uint32_t v_count = 0;

            for (orgQhull::QhullFacet f : qh.facetList()) {
                if (!f.isGood()) continue;

                // 利用 Eigen 获取 Qhull 定义的数学法线（始终朝外）
                orgQhull::QhullHyperplane hp = f.hyperplane();
                Eigen::Vector3<T> outwardNormal(static_cast<T>(hp[0]),
                                                static_cast<T>(hp[1]),
                                                static_cast<T>(hp[2]));

                orgQhull::QhullVertexSet vs = f.vertices();
                std::vector<uint32_t> face_indices;
                std::vector<Eigen::Vector3<T>> face_coords;

                for (orgQhull::QhullVertex v : vs) {
                    int qid = v.id();
                    orgQhull::QhullPoint qp = v.point();
                    Eigen::Vector3<T> pos(static_cast<T>(qp[0]),
                                          static_cast<T>(qp[1]),
                                          static_cast<T>(qp[2]));

                    if (qh_id_to_mesh_idx.find(qid) == qh_id_to_mesh_idx.end()) {
                        resultMesh.vertices.emplace_back(Point3D<T>{pos.x(), pos.y(), pos.z()});
                        qh_id_to_mesh_idx[qid] = v_count++;
                    }
                    face_indices.push_back(qh_id_to_mesh_idx[qid]);
                    face_coords.push_back(pos);
                }

                // 4. 使用 Eigen 进行 Winding Order（环绕顺序）校正
                if (face_indices.size() == 3) {
                    // 计算几何法线: (v1 - v0) x (v2 - v0)
                    Eigen::Vector3<T> side1 = face_coords[1] - face_coords[0];
                    Eigen::Vector3<T> side2 = face_coords[2] - face_coords[0];
                    Eigen::Vector3<T> geomNormal = side1.cross(side2);

                    // 检查几何法线是否与物理外向法线同向
                    if (geomNormal.dot(outwardNormal) >= 0) {
                        // 已经是 CCW (逆时针)，直接压入
                        resultMesh.indices.push_back(face_indices[0]);
                        resultMesh.indices.push_back(face_indices[1]);
                        resultMesh.indices.push_back(face_indices[2]);
                    } else {
                        // 顺时针，翻转为 CCW 压入
                        resultMesh.indices.push_back(face_indices[0]);
                        resultMesh.indices.push_back(face_indices[2]);
                        resultMesh.indices.push_back(face_indices[1]);
                    }
                }
            }

            return resultMesh;
        }
    };
}