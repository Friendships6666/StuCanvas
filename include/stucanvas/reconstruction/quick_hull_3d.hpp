/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once

#include <vector>
#include <cmath>
#include <limits>
#include <queue>
#include <algorithm>

#include "../types/point.hpp"
#include "../types/tri_mesh_halfedge_3d.hpp"
#include "../types/mesh.hpp"

namespace StuCanvas
{
    template <typename T>
    class QuickHull3D
    {
    public:
        using Index = uint32_t;
        static constexpr T EPSILON = static_cast<T>(1e-6); // 容差防止浮点数精度崩溃

        /**
         * @brief 执行 3D QuickHull 算法，生成凸包网格
         * @param points 输入的点云数据
         * @return 生成的水密凸包三角网格 (Mesh3D)
         */
        Mesh3D<T> Compute(const std::vector<Point3D<T>>& points)
        {
            if (points.size() < 4) return Mesh3D<T>(); // 点太少，无法构成 3D 体积

            mesh.Reserve(points.size(), points.size() * 3, points.size() * 2);
            face_infos.clear();

            // 1. 初始化：寻找极值点构建初始四面体
            if (!BuildInitialTetrahedron(points))
            {
                return Mesh3D<T>(); // 点云共面或退化，直接返回空
            }

            // 2. 主循环：利用冲突图不断向外扩展凸包
            while (!active_faces.empty())
            {
                Index curr_face = active_faces.front();
                active_faces.pop();

                // 如果面已经被删除，或没有冲突点，跳过
                if (mesh.faces[curr_face].is_deleted || face_infos[curr_face].conflict_points.empty())
                    continue;

                // 提取距离最远的点（Eye Point）
                Index eye_pt = face_infos[curr_face].farthest_pt_idx;

                // 寻找可见面和地平线
                ProcessEyePoint(eye_pt, curr_face, points);
            }

            // 3. 提取最终结果为 Mesh3D
            return ExtractMesh();
        }

    private:
        // ==========================================
        // 内部数据结构 (冲突图相关)
        // ==========================================

        struct ConflictPoint {
            Index pt_idx;
            T dist; // 未归一化的距离
        };

        struct FaceInfo {
            Point3D<T> normal; // 未归一化的法向量
            std::vector<ConflictPoint> conflict_points;
            T max_dist = -1;
            Index farthest_pt_idx = TriMeshHalfEdge3D<T>::INVALID_IDX;
        };

        TriMeshHalfEdge3D<T> mesh;
        std::vector<FaceInfo> face_infos;
        std::queue<Index> active_faces; // 存有冲突点的活跃面队列

        // ==========================================
        // 向量数学辅助 (内联加速)
        // ==========================================
        inline Point3D<T> Sub(const Point3D<T>& a, const Point3D<T>& b) const {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        inline T Dot(const Point3D<T>& a, const Point3D<T>& b) const {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        inline Point3D<T> Cross(const Point3D<T>& a, const Point3D<T>& b) const {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        inline T LengthSq(const Point3D<T>& a) const {
            return Dot(a, a);
        }

        // ==========================================
        // 核心流程
        // ==========================================

        bool BuildInitialTetrahedron(const std::vector<Point3D<T>>& points)
        {
            // 找 6 个极值点 (MinX, MaxX, MinY, MaxY, MinZ, MaxZ)
            Index ex[6] = {0, 0, 0, 0, 0, 0};
            for (Index i = 1; i < points.size(); ++i) {
                if (points[i].x < points[ex[0]].x) ex[0] = i;
                if (points[i].x > points[ex[1]].x) ex[1] = i;
                if (points[i].y < points[ex[2]].y) ex[2] = i;
                if (points[i].y > points[ex[3]].y) ex[3] = i;
                if (points[i].z < points[ex[4]].z) ex[4] = i;
                if (points[i].z > points[ex[5]].z) ex[5] = i;
            }

            // 寻找距离最远的两个点 V0, V1
            Index v0 = 0, v1 = 0;
            T max_dist_sq = -1;
            for (int i = 0; i < 6; ++i) {
                for (int j = i + 1; j < 6; ++j) {
                    T d = LengthSq(Sub(points[ex[i]], points[ex[j]]));
                    if (d > max_dist_sq) { max_dist_sq = d; v0 = ex[i]; v1 = ex[j]; }
                }
            }
            if (max_dist_sq < EPSILON) return false;

            // 寻找距离直线 V0-V1 最远的点 V2
            Index v2 = 0;
            max_dist_sq = -1;
            Point3D<T> dir01 = Sub(points[v1], points[v0]);
            for (Index i = 0; i < points.size(); ++i) {
                // 叉乘的模长正比于点到直线的距离
                T d = LengthSq(Cross(Sub(points[i], points[v0]), dir01));
                if (d > max_dist_sq) { max_dist_sq = d; v2 = i; }
            }
            if (max_dist_sq < EPSILON) return false;

            // 寻找距离平面 V0-V1-V2 最远的点 V3
            Index v3 = 0;
            T max_dist = -1;
            Point3D<T> plane_n = Cross(Sub(points[v1], points[v0]), Sub(points[v2], points[v0]));
            for (Index i = 0; i < points.size(); ++i) {
                T d = std::abs(Dot(plane_n, Sub(points[i], points[v0]))); // 使用未归一化法向量
                if (d > max_dist) { max_dist = d; v3 = i; }
            }
            if (max_dist < EPSILON) return false;

            // 构建四面体并对齐法线朝外
            Point3D<T> center = {
                (points[v0].x + points[v1].x + points[v2].x + points[v3].x) / 4,
                (points[v0].y + points[v1].y + points[v2].y + points[v3].y) / 4,
                (points[v0].z + points[v1].z + points[v2].z + points[v3].z) / 4
            };

            Index m_v0 = mesh.AddVertex(points[v0]);
            Index m_v1 = mesh.AddVertex(points[v1]);
            Index m_v2 = mesh.AddVertex(points[v2]);
            Index m_v3 = mesh.AddVertex(points[v3]);

            auto AddOrientedFace = [&](Index a, Index b, Index c) {
                Point3D<T> n = Cross(Sub(mesh.vertices[b].position, mesh.vertices[a].position),
                                     Sub(mesh.vertices[c].position, mesh.vertices[a].position));
                Index face_idx = (Dot(n, Sub(mesh.vertices[a].position, center)) < 0) ?
                                 mesh.AddFace(a, c, b) : mesh.AddFace(a, b, c);
                RegisterNewFace(face_idx);
                return face_idx;
            };

            Index f0 = AddOrientedFace(m_v0, m_v1, m_v2);
            Index f1 = AddOrientedFace(m_v0, m_v3, m_v1);
            Index f2 = AddOrientedFace(m_v0, m_v2, m_v3);
            Index f3 = AddOrientedFace(m_v1, m_v3, m_v2);

            // 初始化冲突图：分配所有点
            std::vector<Index> initial_faces = {f0, f1, f2, f3};
            for (Index i = 0; i < points.size(); ++i) {
                if (i == v0 || i == v1 || i == v2 || i == v3) continue;
                AssignPointToFaces(i, points[i], initial_faces);
            }

            return true;
        }

        void ProcessEyePoint(Index eye_pt, Index start_face, const std::vector<Point3D<T>>& points)
        {
            // 1. BFS 寻找所有对 Eye Point 可见的面
            std::vector<Index> visible_faces;
            std::queue<Index> q;
            std::vector<bool> visited(mesh.faces.size(), false);

            q.push(start_face);
            visited[start_face] = true;

            while (!q.empty())
            {
                Index f = q.front(); q.pop();
                visible_faces.push_back(f);

                auto adj = mesh.GetAdjacentFaces(f);
                for (Index n_f : adj) {
                    if (n_f != TriMeshHalfEdge3D<T>::INVALID_IDX && !mesh.faces[n_f].is_deleted && !visited[n_f]) {
                        Index v_start = mesh.GetFaceVertices(n_f)[0];
                        // 距离判定 (未归一化)
                        if (Dot(face_infos[n_f].normal, Sub(points[eye_pt], mesh.vertices[v_start].position)) > EPSILON) {
                            visited[n_f] = true;
                            q.push(n_f);
                        }
                    }
                }
            }

            // 2. 寻找地平线 (Horizon Edges)
            std::vector<std::pair<Index, Index>> horizon_edges;
            for (Index f : visible_faces) {
                Index he0 = mesh.faces[f].edge;
                Index he = he0;
                do {
                    Index twin = mesh.edges[he].twin;
                    Index twin_f = (twin != TriMeshHalfEdge3D<T>::INVALID_IDX) ? mesh.edges[twin].face : TriMeshHalfEdge3D<T>::INVALID_IDX;

                    bool twin_visible = false;
                    if (twin_f != TriMeshHalfEdge3D<T>::INVALID_IDX && !mesh.faces[twin_f].is_deleted) {
                        Index v_start = mesh.GetFaceVertices(twin_f)[0];
                        if (Dot(face_infos[twin_f].normal, Sub(points[eye_pt], mesh.vertices[v_start].position)) > EPSILON) {
                            twin_visible = true;
                        }
                    }

                    if (!twin_visible) {
                        // 地平线边: v0 -> v1
                        Index v0 = mesh.edges[he].origin_vertex;
                        Index v1 = mesh.edges[mesh.edges[he].next].origin_vertex;
                        horizon_edges.push_back({v0, v1});

                        // 【关键】切断旧的 twin 链接，以便新面构建时重新缝合
                        if (twin != TriMeshHalfEdge3D<T>::INVALID_IDX) {
                            mesh.edges[twin].twin = TriMeshHalfEdge3D<T>::INVALID_IDX;
                        }
                    }
                    he = mesh.edges[he].next;
                } while (he != he0);
            }

            // 3. 删除可见面，收集孤儿点
            std::vector<Index> orphans;
            for (Index f : visible_faces) {
                for (const auto& cp : face_infos[f].conflict_points) {
                    if (cp.pt_idx != eye_pt) orphans.push_back(cp.pt_idx);
                }
                face_infos[f].conflict_points.clear();
                mesh.DeleteFace(f);
            }

            // 4. 创建新网格
            Index new_v = mesh.AddVertex(points[eye_pt]);
            std::vector<Index> new_faces;
            for (const auto& edge : horizon_edges) {
                // 原边为 v0->v1 (以洞口视角是逆时针)，新面需要 v1->v0->eye 保持法线向外
                Index nf = mesh.AddFace(edge.second, edge.first, new_v);
                RegisterNewFace(nf);
                new_faces.push_back(nf);
            }

            // 5. 将孤儿点重新分配给新面
            for (Index pt : orphans) {
                AssignPointToFaces(pt, points[pt], new_faces);
            }
        }

        void RegisterNewFace(Index face_idx)
        {
            if (face_idx >= face_infos.size()) {
                face_infos.resize(face_idx + 1);
            }
            auto fvs = mesh.GetFaceVertices(face_idx);
            Point3D<T> p0 = mesh.vertices[fvs[0]].position;
            Point3D<T> p1 = mesh.vertices[fvs[1]].position;
            Point3D<T> p2 = mesh.vertices[fvs[2]].position;

            // 缓存未归一化的法线
            face_infos[face_idx].normal = Cross(Sub(p1, p0), Sub(p2, p0));
            face_infos[face_idx].max_dist = -1;
            face_infos[face_idx].farthest_pt_idx = TriMeshHalfEdge3D<T>::INVALID_IDX;
            face_infos[face_idx].conflict_points.clear();
        }

        void AssignPointToFaces(Index pt_idx, const Point3D<T>& pt_pos, const std::vector<Index>& target_faces)
        {
            for (Index f : target_faces) {
                Index v_start = mesh.GetFaceVertices(f)[0];
                // 使用点乘计算未归一化的有向距离
                T dist = Dot(face_infos[f].normal, Sub(pt_pos, mesh.vertices[v_start].position));

                if (dist > EPSILON) {
                    face_infos[f].conflict_points.push_back({pt_idx, dist});
                    if (dist > face_infos[f].max_dist) {
                        face_infos[f].max_dist = dist;
                        face_infos[f].farthest_pt_idx = pt_idx;
                    }
                    // 分配给第一个可见面就退出 (降低内存与耗时)
                    if (face_infos[f].conflict_points.size() == 1 && dist == face_infos[f].max_dist) {
                        active_faces.push(f);
                    }
                    break;
                }
            }
        }

        Mesh3D<T> ExtractMesh()
        {
            Mesh3D<T> out_mesh;
            // 建立旧顶点索引 -> 新顶点索引的映射
            std::vector<Index> v_map(mesh.vertices.size(), TriMeshHalfEdge3D<T>::INVALID_IDX);
            uint32_t current_idx = 0;

            for (size_t i = 0; i < mesh.faces.size(); ++i) {
                if (!mesh.faces[i].is_deleted) {
                    auto fvs = mesh.GetFaceVertices(i);
                    uint32_t mapped_indices[3];
                    for (int k = 0; k < 3; ++k) {
                        Index v_origin = fvs[k];
                        if (v_map[v_origin] == TriMeshHalfEdge3D<T>::INVALID_IDX) {
                            v_map[v_origin] = current_idx++;
                            // 加入到最终的 Mesh3D
                            out_mesh.vertices.push_back(Vertex3D<T>(mesh.vertices[v_origin].position));
                        }
                        mapped_indices[k] = v_map[v_origin];
                    }
                    out_mesh.AddTriangle(mapped_indices[0], mapped_indices[1], mapped_indices[2]);
                }
            }
            return out_mesh;
        }
    };
}