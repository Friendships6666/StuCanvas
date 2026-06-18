// stucanvas/object/graph.hpp
#pragma once
#include <vector>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <mutex>
#include <limits>
#include <tbb/parallel_for.h>
#include "../utils/flat_map.hpp"
#include "sobject.hpp"
#include "../types/cpu/cpu_types.hpp"
#include "eigen3/Eigen/Dense"
#include "instance.hpp"
#include "tiny_vector.hpp"

namespace StuCanvas
{
    // ========================================================================
    // C++20 指定初始化器配置结构体 (零开销参数包，用于自适应排版与连线)
    // ========================================================================

    template <typename T>
    struct Point2DCreateInfo
    {
        std::string_view name = "FreePoint2D";
        std::vector<const SObject<T>*> parents = {};
    };

    template <typename T>
    struct Point3DCreateInfo
    {
        std::string_view name = "FreePoint3D";
        std::vector<const SObject<T>*> parents = {};
    };


    // ========================================================================
    // 核心对象图谱 (SObjectGraph) —— 100% 稀疏级 O(M) 推送计算图
    // ========================================================================
    enum class AlignBoundary : uint8_t { Min, Center, Max };


    template <typename T>
    struct SObjectGraph
    {
        using value_type = T;


        utils::BlockDeque<SObjectFamily<T>, 64> family_pool;


        // 接口：创建一个高能几何家族，返回只读指针
        const SObjectFamily<T>* createFamily(const SObject<T>* start_node)
        {
            // 在大池中原位构造家族，自动运行 getFamily 爬取并装配成员
            auto& fam = family_pool.emplace_back(*this, start_node);
            return &fam;
        }


        utils::FlatMap<const SObject<T>*, Outline> fonts_2d;
        utils::FlatMap<const SObject<T>*, Point2D_CPU<T>> points_2d;
        utils::FlatMap<const SObject<T>*, Point3D_CPU<T>> points_3d;
        utils::FlatMap<const SObject<T>*, SegmentStrips2D_CPU<T>> segment_stips_2d;
        utils::FlatMap<const SObject<T>*, SegmentStrips3D_CPU<T>> segment_stips_3d;
        utils::FlatMap<const SObject<T>*, Triangles2D_CPU<T>> triangles_2d;
        utils::FlatMap<const SObject<T>*, Triangles3D_CPU<T>> triangles_3d;

        // 内存连续、极其紧凑的对象分配大池（确保 Object* 物理寻址的绝对地址稳定性）
        utils::BlockDeque<SObject<T>, 256> node_pool;

        // 脏节点收集列表
        utils::TinyVector<SObject<T>*> dirty_list;
        utils::TinyVector<SObject<T>*> triangles_required_list;
        utils::TinyVector<SObject<T>*> points_required_list;
        utils::TinyVector<SObject<T>*> strips_required_list;

        // 侧边属性并行数组 (SoA)，彻底免去 SObject 内置索引的空间开销
        utils::TinyVector<uint32_t> topo_indices;
        utils::TinyVector<uint32_t> level_indices;

        utils::TinyVector<utils::TinyVector<SObject<T>*>> cached_levels;
        bool topology_changed = true;


        // 物理寻址安全：辅助函数，获取节点在大池中的物理下标
        size_t GetNodeIndex(const SObject<T>* node) const noexcept
        {
            return node - &node_pool[0];
        }


        SObject<T>* AllocateModel(NodeType type, std::string_view name, bool is_dirty = true);

        // ─── 1. 含有数值参数的创建函数（采用 C++20 可选参数包） ───

        const SObject<T>* createFreePoint2D(T x, T y, const Point2DCreateInfo<T>& info = {});

        const SObject<T>* createFreePoint3D(T x, T y, T z, const Point3DCreateInfo<T>& info = {});

        // ─── 2. 无数值参数的创建函数（保持单个默认名字参数） ───

        const SObject<T>* createSegment2D(const SObject<T>* p1, const SObject<T>* p2,
                                          std::string_view name = "Segment2D");

        const SObject<T>* createStraightLine2D(const SObject<T>* p1, const SObject<T>* p2,
                                               std::string_view name = "StraightLine2D");

        const SObject<T>* createRay2D(const SObject<T>* p1, const SObject<T>* p2,
                                      std::string_view name = "Ray2D");

        const SObject<T>* createSegment3D(const SObject<T>* p1, const SObject<T>* p2,
                                          std::string_view name = "Segment3D");

        const SObject<T>* createStraightLine3D(const SObject<T>* p1, const SObject<T>* p2,
                                               std::string_view name = "StraightLine3D");

        const SObject<T>* createRay3D(const SObject<T>* p1, const SObject<T>* p2,
                                      std::string_view name = "Ray3D");

        const SObject<T>* createPlane3D(const SObject<T>* p1, const SObject<T>* p2, const SObject<T>* p3,
                                        std::string_view name = "Plane3D");

        const SObject<T>* createMidPoint2D(const SObject<T>* p1, const SObject<T>* p2,
                                           std::string_view name = "MidPoint2D");

        const SObject<T>* createMidPoint3D(const SObject<T>* p1, const SObject<T>* p2,
                                           std::string_view name = "MidPoint3D");
        const SObject<T>* createScalar(T value, std::string_view name = "Scalar");

        // ─── 3. 修改函数与拓扑关系动态重组 ───

        void markDirty(SObject<T>* node) noexcept;
        void markRequireTriangles(SObject<T>* node) noexcept;
        void markRequirePoints(SObject<T>* node) noexcept;
        void markRequireStips(SObject<T>* node) noexcept;
        // 修改 2D 点数值，支持通过参数包同时可选更改名称或拓扑
        void modifyPoint2D(const SObject<T>* model_ptr, T new_x, T new_y);

        void modifyScalar(const SObject<T>* model_ptr, T new_value);
        void modifyName(const SObject<T>* model_ptr, std::string_view new_name);
        // 核心亮点：允许在运行期，动态拆除旧连线并重组新的父子依赖（无缝应对 CAD 撤销、动态拼装等需求）
        void modifyParents(const SObject<T>* model_ptr, const std::vector<const SObject<T>*>& new_parents);

        const SObjectData<T>& GetResultData(const SObject<T>* node_ptr);

        // ========================================================================
        // O(M) 推送式解算引擎
        // ========================================================================
        void Compute(bool use_parallel = true)
        {
            if (dirty_list.empty()) return;

            // 1. 稀疏自适应向下脏化
            PropagateDirtyFlags();

            if (!triangles_required_list.empty()) PropagateTrianglesRequiredFlags();

            if (!points_required_list.empty()) PropagatePointsRequiredFlags();

            if (!strips_required_list.empty()) PropagateStripsRequiredFlags();

            // 2. 若拓扑改变，重新编译层级并填充侧边属性
            if (topology_changed)
            {
                CompileLeveledOrder();
                for (size_t i = 0; i < family_pool.size(); ++i)
                {
                    family_pool[i].refresh(); // 自动重新爬取拓扑网络，补齐成员
                }
                topology_changed = false;
            }

            // 3. 排序：利用外部侧边 SoA 数组进行局部脏节点的拓扑排序 (O(M log M))
            std::sort(dirty_list.begin(), dirty_list.end(), [this](const SObject<T>* a, const SObject<T>* b)
            {
                return topo_indices[GetNodeIndex(a)] < topo_indices[GetNodeIndex(b)];
            });

            // 4. 执行解算
            if (use_parallel)
            {
                size_t i = 0;
                while (i < dirty_list.size())
                {
                    size_t j = i + 1;
                    size_t current_level_idx = level_indices[GetNodeIndex(dirty_list[i])];

                    while (j < dirty_list.size() && level_indices[GetNodeIndex(dirty_list[j])] == current_level_idx)
                    {
                        j++;
                    }

                    // oneTBB 层级并行保护
                    tbb::parallel_for(i, j, [&](size_t idx)
                    {
                        SObject<T>* node = dirty_list[idx];
                        if (node->vptr && node->vptr->solver)
                        {
                            node->vptr->solver(*this, *node);
                        }
                    });

                    i = j;
                }
            }
            else
            {
                // 单核心拓扑流式连续解算
                for (auto* node : dirty_list)
                {
                    if (node->vptr && node->vptr->solver)
                    {
                        node->vptr->solver(*this, *node);
                    }
                }
            }

            // 5. 状态重置与收尾
            for (auto* node : dirty_list)
            {
                node->clear_mask(NodeMask::DIRTY);
            }
            dirty_list.clear();
        }

    private:
        void PropagateDirtyFlags()
        {
            size_t head = 0;
            while (head < dirty_list.size())
            {
                SObject<T>* curr = dirty_list[head];
                head++;

                for (auto* child : curr->children)
                {
                    if (child && !child->has_mask(NodeMask::DIRTY))
                    {
                        child->set_mask(NodeMask::DIRTY);
                        dirty_list.push_back(child);
                    }
                }
            }
        }

        void PropagateTrianglesRequiredFlags()
        {
            size_t head = 0;
            while (head < triangles_required_list.size())
            {
                SObject<T>* curr = triangles_required_list[head];
                head++;

                for (auto* parent : curr->parents)
                {
                    parent->set_mask(NodeMask::TRIANGLES_GENERATOR);
                }
            }
            triangles_required_list.clear();
        }

        void PropagateStripsRequiredFlags()
        {
            size_t head = 0;
            while (head < strips_required_list.size())
            {
                SObject<T>* curr = strips_required_list[head];
                head++;

                for (auto* parent : curr->parents)
                {
                    parent->set_mask(NodeMask::STRIPS_GENERATOR);
                }
            }
            strips_required_list.clear();
        }

        void PropagatePointsRequiredFlags()
        {
            size_t head = 0;
            while (head < points_required_list.size())
            {
                SObject<T>* curr = points_required_list[head];
                head++;

                for (auto* parent : curr->parents)
                {
                    parent->set_mask(NodeMask::POINTS_GENERATOR);
                }
            }
            points_required_list.clear();
        }


        void CompileLeveledOrder()
        {
            cached_levels.clear();
            if (node_pool.empty()) return;

            topo_indices.resize(node_pool.size());
            level_indices.resize(node_pool.size());

            utils::TinyVector<size_t> in_degrees(node_pool.size(), 0);
            utils::TinyVector<SObject<T>*> current_level;

            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                auto& node = node_pool[i];
                size_t d = node.parents.size();
                in_degrees[i] = d;
                if (d == 0)
                {
                    current_level.push_back(&node);
                }
            }

            size_t global_topo_counter = 0;
            size_t current_level_idx = 0;
            while (!current_level.empty())
            {
                cached_levels.push_back(current_level);
                utils::TinyVector<SObject<T>*> next_level;

                for (auto* curr : current_level)
                {
                    size_t curr_idx = GetNodeIndex(curr);

                    topo_indices[curr_idx] = static_cast<uint32_t>(global_topo_counter++);
                    level_indices[curr_idx] = static_cast<uint32_t>(current_level_idx);

                    for (auto* child : curr->children)
                    {
                        size_t child_idx = GetNodeIndex(child);
                        if (--in_degrees[child_idx] == 0)
                        {
                            next_level.push_back(child);
                        }
                    }
                }
                current_level = std::move(next_level);
                current_level_idx++;
            }
        }

        utils::TinyVector<SObject<T>*> getFamily(const SObject<T>* start_node)
        {
            utils::TinyVector<SObject<T>*> family;

            // 1. 预分配小空间，避免小规模图频繁触发 vector 内部扩容
            family.reserve(32);

            // 强转为非 const 指针，以便我们临时读写 VISITED 状态掩码
            auto* start = const_cast<SObject<T>*>(start_node);

            // 2. 声明一维工作队列（直接利用连续内存提升 Cache 命中率）
            utils::TinyVector<SObject<T>*> queue;
            queue.reserve(32);

            // 将起点压入，并标记为已访问，防止回溯
            queue.push_back(start);
            start->set_mask(NodeMask::VISITED);

            size_t head = 0;
            while (head < queue.size())
            {
                SObject<T>* curr = queue[head];
                head++;

                // 归入家族列表
                family.push_back(curr);

                // ─── A. 向上追溯所有的父亲（双向依存搜寻） ───
                for (size_t i = 0; i < curr->parents.size(); ++i)
                {
                    auto* parent = const_cast<SObject<T>*>(curr->parents[i]);
                    if (parent && !parent->has_mask(NodeMask::VISITED))
                    {
                        parent->set_mask(NodeMask::VISITED);
                        queue.push_back(parent); // 加入探索队列
                    }
                }

                // ─── B. 向下追溯所有的孩子 ───
                for (size_t i = 0; i < curr->children.size(); ++i)
                {
                    auto* child = const_cast<SObject<T>*>(curr->children[i]);
                    if (child && !child->has_mask(NodeMask::VISITED))
                    {
                        child->set_mask(NodeMask::VISITED);
                        queue.push_back(child); // 加入探索队列
                    }
                }
            }

            // 3. 【极速扫尾】：擦除这家人身上的所有临时 VISITED 标记，100% 恢复节点纯净状态
            for (auto* node : family)
            {
                node->clear_mask(NodeMask::VISITED);
            }

            return family;
        }
    };
} // namespace StuCanvas
