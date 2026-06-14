// stucanvas/object/graph.hpp
#pragma once
#include <vector>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <mutex>
#include <tbb/parallel_for.h>
#include "../utils/flat_map.hpp"
#include "sobject.hpp"
#include "../types/cpu/cpu_types.hpp"

namespace StuCanvas
{
    // ========================================================================
    // C++20 指定初始化器配置结构体 (零开销参数包，用于自适应排版与连线)
    // ========================================================================

    template <typename T>
    struct Point2DCreateInfo {
        std::string_view name = "FreePoint2D";
        std::vector<const SObject<T>*> parents = {};
    };

    template <typename T>
    struct Point3DCreateInfo {
        std::string_view name = "FreePoint3D";
        std::vector<const SObject<T>*> parents = {};
    };




    // ========================================================================
    // 核心对象图谱 (SObjectGraph) —— 100% 稀疏级 O(M) 推送计算图
    // ========================================================================
    template <typename T>
    struct SObjectGraph
    {
        using value_type = T;

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
        std::vector<SObject<T>*> dirty_list;

        // 侧边属性并行数组 (SoA)，彻底免去 SObject 内置索引的空间开销
        std::vector<uint32_t> topo_indices;
        std::vector<uint32_t> level_indices;

        std::vector<std::vector<SObject<T>*>> cached_levels;
        bool topology_changed = true;



        // 物理寻址安全：辅助函数，获取节点在大池中的物理下标
        size_t GetNodeIndex(const SObject<T>* node) const noexcept
        {
            return node - &node_pool[0];
        }


        SObject<T>* AllocateModel(NodeType type, std::string_view name);

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

        const SObject<T>* createMidPoint2D(const SObject<T>* p1, const SObject<T>* p2, std::string_view name = "MidPoint2D");

        const SObject<T>* createMidPoint3D(const SObject<T>* p1, const SObject<T>* p2, std::string_view name = "MidPoint3D");
        const SObject<T>* createScalar(T value, std::string_view name = "Scalar");

        // ─── 3. 修改函数与拓扑关系动态重组 ───

        void markDirty(SObject<T>* node) noexcept;
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

            // 2. 若拓扑改变，重新编译层级并填充侧边属性
            if (topology_changed)
            {
                CompileLeveledOrder();
                topology_changed = false;
            }

            // 3. 排序：利用外部侧边 SoA 数组进行局部脏节点的拓扑排序 (O(M log M))
            std::sort(dirty_list.begin(), dirty_list.end(), [this](const SObject<T>* a, const SObject<T>* b) {
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

        void CompileLeveledOrder()
        {
            cached_levels.clear();
            if (node_pool.empty()) return;

            topo_indices.resize(node_pool.size());
            level_indices.resize(node_pool.size());

            std::vector<size_t> in_degrees(node_pool.size(), 0);
            std::vector<SObject<T>*> current_level;

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
                std::vector<SObject<T>*> next_level;

                for (auto* curr : current_level)
                {
                    size_t curr_idx = GetNodeIndex(curr);

                    topo_indices[curr_idx]  = static_cast<uint32_t>(global_topo_counter++);
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
    };






} // namespace StuCanvas