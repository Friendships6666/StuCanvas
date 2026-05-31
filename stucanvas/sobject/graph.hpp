// ============================================================================
// stucanvas/object/graph.hpp
// ============================================================================
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
    enum class ExecutionMode : uint8_t
    {
        Pull, // 稀疏模式：JIT 延迟堆栈迭代拉取
        Push // 稠密模式：Kahn 算法 oneTBB 并行推送解算
    };


    // ========================================================================
    // 核心对象图谱 (ObjectGraph) —— 100% 纯指针级高能代数计算图
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

        std::vector<std::vector<SObject<T>*>> cached_levels;
        bool topology_changed = true;

        struct FrameStats
        {
            size_t dirty_count = 0;
            size_t query_count = 0;
        };

        FrameStats last_frame_stats{0, 0};
        FrameStats curr_frame_stats{0, 0};

        float smooth_query_ratio = 0.0f;
        ExecutionMode current_mode = ExecutionMode::Pull;

        void NewFrame() noexcept
        {
            last_frame_stats = curr_frame_stats;
            curr_frame_stats = {0, 0};

            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                node_pool[i].clear_mask(NodeMask::QUERIED);
            }

            size_t total_nodes = node_pool.size();
            double dirty_ratio = static_cast<double>(last_frame_stats.dirty_count) / std::max<size_t>(total_nodes, 1);
            double query_ratio = static_cast<double>(last_frame_stats.query_count) / std::max<size_t>(total_nodes, 1);

            const float alpha = 0.2f;
            smooth_query_ratio = alpha * static_cast<float>(query_ratio) + (1.0f - alpha) * smooth_query_ratio;

            if (total_nodes < 128)
            {
                current_mode = ExecutionMode::Pull;
            }
            else if (dirty_ratio > 0.3 || query_ratio > 0.5)
            {
                current_mode = ExecutionMode::Push;
            }
            else
            {
                current_mode = ExecutionMode::Pull;
            }
        }

        SObject<T>* AllocateModel(NodeType type, std::string_view name)
        {
            auto& node = node_pool.emplace_back();
            node.type = type;
            node.name = name;
            node.graph = this;
            node.set_mask(NodeMask::DIRTY);
            topology_changed = true;
            return &node;
        }

        const SObject<T>* createFreePoint2D(T x, T y, std::string_view name = "FreePoint2D")
        {
            SObject<T>* node = AllocateModel(NodeType::POINT_2D_FREE, name);
            node->data.point_2d.x = x;
            node->data.point_2d.y = y;
            node->vptr = &Point2DFree_VTable<T>;
            return node;
        }

        const SObject<T>* createStraightLine2D(const SObject<T>* p1, const SObject<T>* p2,
                                               std::string_view name = "StraightLine2D")
        {
            SObject<T>* node = AllocateModel(NodeType::LINE_2D_STRAIGHT, name);
            node->parents.push_back(p1);
            node->parents.push_back(p2);
            node->vptr = &Line2DStraight_VTable<T>;

            // 双向依存连线
            const_cast<SObject<T>*>(p1)->children.push_back(node);
            const_cast<SObject<T>*>(p2)->children.push_back(node);


            return node;
        }

        const SObject<T>* createPlane3D(
            const SObject<T>* p1,
            const SObject<T>* p2,
            const SObject<T>* p3,
            std::string_view name = "Plane3D") // [3.2.1]
        {
            // 1. 物理分配节点，类型设为 PLANE_3D [3.2.1]
            SObject<T>* node = AllocateModel(NodeType::PLANE_3D, name);

            // 2. 将 3 个 3D 点作为父节点绑定 [3.2.1]
            node->parents.push_back(p1);
            node->parents.push_back(p2);
            node->parents.push_back(p3);

            // 3. 绑定平面的物理 VTable [3.2.1]
            node->vptr = &Plane3D_VTable<T>;

            // 4. 建立双向依存连线 (让 3 个点发生变更时能向上脏化该平面) [3.2.1]
            const_cast<SObject<T>*>(p1)->children.push_back(node);
            const_cast<SObject<T>*>(p2)->children.push_back(node);
            const_cast<SObject<T>*>(p3)->children.push_back(node);


            return node;
        }

        void modifyPoint(const SObject<T>* model_ptr, T new_x, T new_y)
        {
            if (!model_ptr) return;
            auto* node = const_cast<SObject<T>*>(model_ptr);
            if (node->data.point_2d.x != new_x || node->data.point_2d.y != new_y)
            {
                node->data.point_2d.x = new_x;
                node->data.point_2d.y = new_y;
                node->set_mask(NodeMask::DIRTY); // 仅脏化自身
                curr_frame_stats.dirty_count++; // 物理增量计数
            }
        }


        const SObjectData<T>& GetResultData(const SObject<T>* node_ptr)
        {
            if (!node_ptr) throw std::invalid_argument("Null pointer");

            if (!node_ptr->has_mask(NodeMask::QUERIED))
            {
                node_ptr->set_mask(NodeMask::QUERIED);
                curr_frame_stats.query_count++;
            }

            // 智能自适应调度
            if (current_mode == ExecutionMode::Push)
            {
                if (curr_frame_stats.dirty_count > 0)
                {
                    ExecutePushModelParallel();
                    curr_frame_stats.dirty_count = 0;
                }
            }
            else
            {
                EvaluateModelJIT(const_cast<SObject<T>*>(node_ptr));
            }

            // 一等公民寻址：直接以极致的 $O(1)$ 指针偏移读取最底层的代数数据！
            return node_ptr->data;
        }

    private:
        void CompileLeveledOrder()
        {
            cached_levels.clear();
            if (node_pool.empty()) return;

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

            while (!current_level.empty())
            {
                cached_levels.push_back(current_level);
                std::vector<SObject<T>*> next_level;

                for (auto* curr : current_level)
                {
                    for (auto* child : curr->children)
                    {
                        size_t child_idx = child - &node_pool[0];
                        if (--in_degrees[child_idx] == 0)
                        {
                            next_level.push_back(child);
                        }
                    }
                }
                current_level = std::move(next_level);
            }
        }

        void ExecutePushModelParallel()
        {
            if (topology_changed)
            {
                CompileLeveledOrder();
                topology_changed = false;
            }

            for (auto& level_nodes : cached_levels)
            {
                tbb::parallel_for(size_t(0), level_nodes.size(), [&](size_t i)
                {
                    EvaluatePushNode(level_nodes[i]);
                });
            }

            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                node_pool[i].clear_mask(NodeMask::RECALCULATED);
            }
        }

        void EvaluatePushNode(SObject<T>* node) noexcept
        {
            bool parent_dirty = false;
            for (const auto* parent : node->parents)
            {
                if (parent && parent->has_mask(NodeMask::RECALCULATED))
                {
                    parent_dirty = true;
                    break;
                }
            }

            if (node->has_mask(NodeMask::DIRTY) || parent_dirty)
            {
                if (node->vptr && node->vptr->solver)
                {
                    node->vptr->solver(*this, *node); // 仅运行代数解算，写入 node->data
                }
                node->clear_mask(NodeMask::DIRTY);
                node->set_mask(NodeMask::RECALCULATED);
            }
        }

        // 极致安全的堆递归 JIT 逆向惰性求值核心 (100% 免疫 Stack Overflow)
        bool EvaluateModelJIT(SObject<T>* root_model)
        {
            if (!root_model) return false;

            struct StackFrame
            {
                SObject<T>* node;
                size_t parent_index;
            };

            std::vector<StackFrame> dfs_stack;
            std::vector<SObject<T>*> ordered_nodes;

            dfs_stack.reserve(128);
            ordered_nodes.reserve(128);

            dfs_stack.push_back({root_model, 0});
            root_model->set_mask(NodeMask::VISITED);

            while (!dfs_stack.empty())
            {
                auto& frame = dfs_stack.back();
                SObject<T>* curr = frame.node;

                if (frame.parent_index < curr->parents.size())
                {
                    const SObject<T>* parent_const = curr->parents[frame.parent_index];
                    frame.parent_index++;

                    SObject<T>* parent = const_cast<SObject<T>*>(parent_const);
                    if (parent && !parent->has_mask(NodeMask::VISITED))
                    {
                        parent->set_mask(NodeMask::VISITED);
                        dfs_stack.push_back({parent, 0});
                    }
                }
                else
                {
                    ordered_nodes.push_back(curr);
                    dfs_stack.pop_back();
                }
            }

            bool root_recalculated = false;

            for (auto* node : ordered_nodes)
            {
                bool parent_recalculated = false;

                for (const auto* parent : node->parents)
                {
                    if (parent && parent->has_mask(NodeMask::RECALCULATED))
                    {
                        parent_recalculated = true;
                        break;
                    }
                }

                if (node->has_mask(NodeMask::DIRTY) || parent_recalculated)
                {
                    if (node->vptr && node->vptr->solver)
                    {
                        node->vptr->solver(*this, *node); // 仅在 CPU 内部执行代数求解，100% 避免耗时离散化
                    }
                    node->clear_mask(NodeMask::DIRTY);
                    node->set_mask(NodeMask::RECALCULATED);

                    if (node == root_model)
                    {
                        root_recalculated = true;
                    }
                }
            }

            for (auto* node : ordered_nodes)
            {
                node->clear_mask(NodeMask::VISITED);
                node->clear_mask(NodeMask::RECALCULATED);
            }

            return root_recalculated;
        }
    };
} // namespace StuCanvas
