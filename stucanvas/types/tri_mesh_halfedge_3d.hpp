// include/stucanvas/types/tri_mesh_halfedge_3d.hpp
#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <limits>
#include <optional>
#include "point.hpp"

namespace StuCanvas
{
    /**
     * @brief 3D 三角网格半边结构
     * 专为随机增量凸包等需要频繁拓扑查询的算法设计。
     * 使用 32 位索引代替指针，保证容器扩容时引用的有效性。
     */
    template <typename T>
    class TriMeshHalfEdge3D
    {
    public:
        using Scalar = T;
        using Index = uint32_t;
        static constexpr Index INVALID_IDX = std::numeric_limits<Index>::max();
        // 前向声明句柄结构
        struct Vertex;
        struct HalfEdge;
        struct Face;
        // ==========================================
        // 核心数据结构定义
        // ==========================================
        struct Vertex
        {
            Point3D<T> position = {0, 0, 0};
            Index outgoing_he = INVALID_IDX; // 从该顶点出发的任意一条半边
        };

        struct HalfEdge
        {
            Index origin_vertex = INVALID_IDX; // 起点顶点索引
            Index face = INVALID_IDX; // 所属面索引
            Index next = INVALID_IDX; // 面内的下一条半边
            Index prev = INVALID_IDX; // 面内的上一条半边
            Index twin = INVALID_IDX; // 对向半边 (邻接面的共享边)
        };

        struct Face
        {
            Index edge = INVALID_IDX; // 面内的任意一条半边 (通常是第一条)
            bool is_deleted = false; // 逻辑删除标记，增量凸包算法核心需求
        };

        // ==========================================
        // 容器存储
        // ==========================================
        std::vector<Vertex> vertices;
        std::vector<HalfEdge> edges;
        std::vector<Face> faces;

    public:
        TriMeshHalfEdge3D() = default;
        // 预分配内存，凸包算法通常可以预估点数
        void Reserve(size_t vert_count, size_t edge_count, size_t face_count)
        {
            vertices.reserve(vert_count);
            edges.reserve(edge_count);
            faces.reserve(face_count);
        }

        // ==========================================
        // 构建接口 (供增量凸包算法调用)
        // ==========================================
        /**
         * @brief 添加一个独立的顶点
         * @return 新顶点的索引
         */
        Index AddVertex(const Point3D<T>& pos)
        {
            vertices.push_back({pos, INVALID_IDX});
            return static_cast<Index>(vertices.size() - 1);
        }

        /**
         * @brief 根据三个顶点索引添加一个三角形面
         * 自动处理半边的 next/prev/twin 连接。
         * @param v0 顶点0索引
         * @param v1 顶点1索引
         * @param v2 顶点2索引
         * @return 新面的索引
         */
        Index AddFace(Index v0, Index v1, Index v2)
        {
            // 创建面
            faces.push_back({INVALID_IDX, false});
            Index face_idx = static_cast<Index>(faces.size() - 1);
            // 创建三条半边
            Index he0 = AddHalfEdge(v0, face_idx);
            Index he1 = AddHalfEdge(v1, face_idx);
            Index he2 = AddHalfEdge(v2, face_idx);
            // 链接 next/prev
            edges[he0].next = he1;
            edges[he0].prev = he2;
            edges[he1].next = he2;
            edges[he1].prev = he0;
            edges[he2].next = he0;
            edges[he2].prev = he1;
            // 绑定面和顶点
            faces[face_idx].edge = he0;
            vertices[v0].outgoing_he = he0;
            vertices[v1].outgoing_he = he1;
            vertices[v2].outgoing_he = he2;
            // 尝试链接 twin (寻找已有反向边)
            LinkTwin(he0);
            LinkTwin(he1);
            LinkTwin(he2);
            return face_idx;
        }

        /**
         * @brief 逻辑删除一个面 (增量凸包核心操作)
         * 标记为删除，但在半边结构中仍保留其拓扑，以便查找 Horizon 边界。
         */
        void DeleteFace(Index face_idx)
        {
            if (face_idx < faces.size())
            {
                faces[face_idx].is_deleted = true;
            }
        }

        // ==========================================
        // 拓扑查询接口 (供凸包算法使用)
        // ==========================================
        /**
         * @brief 获取面的三个顶点索引
         */
        std::array<Index, 3> GetFaceVertices(Index face_idx) const
        {
            Index he0 = faces[face_idx].edge;
            Index he1 = edges[he0].next;
            Index he2 = edges[he1].next;
            return {edges[he0].origin_vertex, edges[he1].origin_vertex, edges[he2].origin_vertex};
        }

        /**
         * @brief 获取与面相邻的三个面索引 (通过 twin 半边跨越)
         * 如果是边界，则对应位置返回 INVALID_IDX
         */
        std::array<Index, 3> GetAdjacentFaces(Index face_idx) const
        {
            Index he0 = faces[face_idx].edge;
            Index he1 = edges[he0].next;
            Index he2 = edges[he1].next;
            return {
                (edges[he0].twin != INVALID_IDX) ? edges[edges[he0].twin].face : INVALID_IDX,
                (edges[he1].twin != INVALID_IDX) ? edges[edges[he1].twin].face : INVALID_IDX,
                (edges[he2].twin != INVALID_IDX) ? edges[edges[he2].twin].face : INVALID_IDX
            };
        }

        /**
         * @brief 判断一条边是否为边界边 (即没有 twin)
         */
        bool IsBoundaryEdge(Index edge_idx) const
        {
            return edges[edge_idx].twin == INVALID_IDX;
        }

    private:
        /**
         * @brief 内部添加半边
         */
        Index AddHalfEdge(Index origin_v, Index face_idx)
        {
            edges.push_back({origin_v, face_idx, INVALID_IDX, INVALID_IDX, INVALID_IDX});
            return static_cast<Index>(edges.size() - 1);
        }

        /**
         * @brief 尝试为新创建的半边寻找 twin 并链接
         * 算法：寻找终点相同且方向相反的无 twin 半边。
         * 在凸包扩展阶段，这保证了地平线边缘能被正确缝合。
         */
        void LinkTwin(Index he_idx)
        {
            auto& he = edges[he_idx];
            Index target_v = edges[he.next].origin_vertex; // 当前半边的终点
            // 遍历目标顶点的出发边，寻找反向边
            Index opp_he = vertices[target_v].outgoing_he;
            if (opp_he != INVALID_IDX)
            {
                // 优化：如果存在对向边，它的终点必然是当前边的起点
                if (edges[opp_he].twin == INVALID_IDX && edges[edges[opp_he].next].origin_vertex == he.origin_vertex)
                {
                    he.twin = opp_he;
                    edges[opp_he].twin = he_idx;
                    return;
                }
                // 简单的线性遍历寻找 (对于凸包构建通常足够快，因为顶点度数有限)
                // 复杂情况可替换为基于 EdgeMap 的 O(1) 查找
                Index curr = edges[opp_he].next; // 转一圈找
                while (curr != opp_he)
                {
                    curr = edges[edges[curr].twin].next; // 跳到下一个边
                    // 防御性编程
                    if (curr == INVALID_IDX || edges[curr].twin == INVALID_IDX) break;
                    if (edges[curr].twin == INVALID_IDX && edges[edges[curr].next].origin_vertex == he.origin_vertex)
                    {
                        he.twin = curr;
                        edges[curr].twin = he_idx;
                        return;
                    }
                }
            }
        }
    };
}
