/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

#pragma once
#include <vector>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <mutex>
#include <tbb/parallel_for.h> // 引入 Intel oneTBB 并行库
#include <../utils/flat_map.hpp>
#include "object.hpp" // Object 对象从独立文件 object.hpp 引入

namespace StuCanvas {

    enum class ExecutionMode : uint8_t {
        Pull, // 稀疏模式：JIT 延迟堆栈迭代拉取
        Push  // 稠密模式：Kahn 算法 oneTBB 并行推送解算
    };

    // ========================================================================
    // 核心对象图谱 (ObjectGraph) —— 100% 纯指针级高能代数计算图
    // ========================================================================
    template <typename T>
    struct ObjectGraph {
        using value_type = T;


        // 内存连续且稳定的对象分配大池，无任何外部堆碎片
        utils::BlockDeque<Object<T>, 256> node_pool;

        // ---- 1. 拓扑分层缓存 (消灭运行期拓扑排序开销) ----
        std::vector<std::vector<Object<T>*>> cached_levels;
        bool topology_changed = true; // 仅在增删节点、改变连线时标记为 true

        // ---- 2. 启发式反馈自检指标 ----
        struct FrameStats {
            size_t dirty_count = 0; // 本帧变脏（被修改）的节点数
            size_t query_count = 0; // 本帧被 unique GetResult 查询的节点数
        };
        FrameStats last_frame_stats{0, 0};
        FrameStats curr_frame_stats{0, 0};

        float smooth_query_ratio = 0.0f; // 经过 EMA 平滑后的查询率
        ExecutionMode current_mode = ExecutionMode::Pull; // 当前帧的计算模态

        // ========================================================================
        // 核心一：每一帧开始时，自动执行启发式决策 (避免 manual 重置漏洞)
        // ========================================================================
        void NewFrame() noexcept {
            // 备份上一帧数据，重置当前帧度量
            last_frame_stats = curr_frame_stats;
            curr_frame_stats = {0, 0};

            // 清除上一帧的查询去重标记
            for (size_t i = 0; i < node_pool.size(); ++i) {
                node_pool[i].clear_mask(NodeMask::QUERIED);
            }

            size_t total_nodes = node_pool.size();
            double dirty_ratio = static_cast<double>(last_frame_stats.dirty_count) / std::max<size_t>(total_nodes, 1);
            double query_ratio = static_cast<double>(last_frame_stats.query_count) / std::max<size_t>(total_nodes, 1);

            // 指数移动平均 (EMA) 算法平滑过滤，防止模式切换颤噪 (0.2 代表当前帧权重)
            const float alpha = 0.2f;
            smooth_query_ratio = alpha * static_cast<float>(query_ratio) + (1.0f - alpha) * smooth_query_ratio;

            // 决策分支：
            //   - 节点数少于 128 时强制 Pull（避免多线程开销）
            //   - 如果脏率 > 30% 或 查询率 > 50%，切换为并行 Push
            if (total_nodes < 128) {
                current_mode = ExecutionMode::Pull;
            } else if (dirty_ratio > 0.3 || query_ratio > 0.5) {
                current_mode = ExecutionMode::Push;
            } else {
                current_mode = ExecutionMode::Pull;
            }
        }

        // 统一的底层物理分配器
        Object<T>* AllocateModel(NodeType type, std::string_view name) {
            auto& node = node_pool.emplace_back();
            node.type = type;
            node.name = name;
            node.graph = this;
            node.set_mask(NodeMask::DIRTY);
            topology_changed = true; // 拓扑结构改变，标记需重排
            return &node;
        }

        const Object<T>* createFreePoint2D(T x, T y, std::string_view name = "FreePoint2D") {
            Object<T>* node = AllocateModel(NodeType::POINT_2D_FREE, name);
            node->data.point_2d.x = x;
            node->data.point_2d.y = y;
            node->vptr = &Point2DFree_VTable<T>;
            return node;
        }

        const Object<T>* createStraightLine2D(const Object<T>* p1, const Object<T>* p2, std::string_view name = "StraightLine2D") {
            Object<T>* node = AllocateModel(NodeType::LINE_2D_STRAIGHT, name);
            node->parents.push_back(p1);
            node->parents.push_back(p2);
            node->vptr = &Line2DStraight_VTable<T>;

            // 双向依存连线
            const_cast<Object<T>*>(p1)->children.push_back(node);
            const_cast<Object<T>*>(p2)->children.push_back(node);


            return node;
        }

        const Object<T>* createPlane3D(
            const Object<T>* p1,
            const Object<T>* p2,
            const Object<T>* p3,
            std::string_view name = "Plane3D") // [3.2.1]
        {
            // 1. 物理分配节点，类型设为 PLANE_3D [3.2.1]
            Object<T>* node = AllocateModel(NodeType::PLANE_3D, name);

            // 2. 将 3 个 3D 点作为父节点绑定 [3.2.1]
            node->parents.push_back(p1);
            node->parents.push_back(p2);
            node->parents.push_back(p3);

            // 3. 绑定平面的物理 VTable [3.2.1]
            node->vptr = &Plane3D_VTable<T>;

            // 4. 建立双向依存连线 (让 3 个点发生变更时能向上脏化该平面) [3.2.1]
            const_cast<Object<T>*>(p1)->children.push_back(node);
            const_cast<Object<T>*>(p2)->children.push_back(node);
            const_cast<Object<T>*>(p3)->children.push_back(node);

            // 5. 标记拓扑结构改变，强制重构分层编译缓存 [3.2.1]


            return node;
        }

        void modifyPoint(const Object<T>* model_ptr, T new_x, T new_y) {
            if (!model_ptr) return;
            auto* node = const_cast<Object<T>*>(model_ptr);
            if (node->data.point_2d.x != new_x || node->data.point_2d.y != new_y) {
                node->data.point_2d.x = new_x;
                node->data.point_2d.y = new_y;
                node->set_mask(NodeMask::DIRTY); // 仅脏化自身
                curr_frame_stats.dirty_count++; // 物理增量计数
            }
        }

        // 动态拓扑连线更新器 (用于在运行时改变依存链)
        void UpdateParentDependency(Object<T>* child, const Object<T>* old_parent, const Object<T>* new_parent) noexcept {
            if (old_parent == new_parent) return;

            // 移除旧连线
            if (old_parent) {
                auto* op = const_cast<Object<T>*>(old_parent);
                std::erase(op->children, child);
            }

            // 建立新连线
            if (new_parent) {
                auto* np = const_cast<Object<T>*>(new_parent);
                np->children.push_back(child);
            }
            topology_changed = true;
        }

        // ==========================================
        // 数据拉取接口 (JIT 逆向按需重算 / 支持 O(1) 快速返回)
        // ==========================================

        const std::vector<Point3D<T>>& GetResultPoints3D(const Object<T>* node_ptr) {
            if (!node_ptr) throw std::invalid_argument("Null pointer");

            // 查询去重计数
            if (!node_ptr->has_mask(NodeMask::QUERIED)) {
                node_ptr->set_mask(NodeMask::QUERIED);
                curr_frame_stats.query_count++;
            }

            if (current_mode == ExecutionMode::Push) {
                // 如果是推模式，且本帧尚未执行过全局并行计算，立刻执行
                if (curr_frame_stats.dirty_count > 0) {
                    ExecutePushModelParallel();
                    curr_frame_stats.dirty_count = 0; // 重置写计数
                }
                return node_ptr->result_points_3d; // O(1) 物理级快速返回
            } else {
                // 纯拉模式下，执行 JIT 堆栈迭代求值
                EvaluateModelJIT(const_cast<Object<T>*>(node_ptr));
                return node_ptr->result_points_3d;
            }
        }

        const std::vector<SegmentStrip3D<T>>& GetResultStrips(const Object<T>* node_ptr) {
            if (!node_ptr) throw std::invalid_argument("Null pointer");

            if (!node_ptr->has_mask(NodeMask::QUERIED)) {
                node_ptr->set_mask(NodeMask::QUERIED);
                curr_frame_stats.query_count++;
            }

            if (current_mode == ExecutionMode::Push) {
                if (curr_frame_stats.dirty_count > 0) {
                    ExecutePushModelParallel();
                    curr_frame_stats.dirty_count = 0;
                }
                return node_ptr->result_strips;
            } else {
                EvaluateModelJIT(const_cast<Object<T>*>(node_ptr));
                return node_ptr->result_strips;
            }
        }

    private:
        // 基于“入度减少”的并行 Kahn 拓扑层次编译器 (Push 模式专用)
        void CompileLeveledOrder() {
            cached_levels.clear();
            if (node_pool.empty()) return;

            std::vector<size_t> in_degrees(node_pool.size(), 0);
            std::vector<Object<T>*> current_level;

            for (size_t i = 0; i < node_pool.size(); ++i) {
                auto& node = node_pool[i];
                if (!node.active) continue;
                size_t d = node.parents.size();
                in_degrees[i] = d;
                if (d == 0) {
                    current_level.push_back(&node);
                }
            }

            while (!current_level.empty()) {
                cached_levels.push_back(current_level);
                std::vector<Object<T>*> next_level;

                for (auto* curr : current_level) {
                    for (auto* child : curr->children) {
                        size_t child_idx = child - &node_pool[0];
                        if (--in_degrees[child_idx] == 0) {
                            next_level.push_back(child);
                        }
                    }
                }
                current_level = std::move(next_level);
            }
        }

        // 全局多线程并行推模型解算器
        void ExecutePushModelParallel() {
            // 1. 仅在拓扑结构切实改变时，才重构 Kahn 序列 (保持 O(V+E) 开销在 Warm Path 下为 0)
            if (topology_changed) {
                CompileLeveledOrder();
                topology_changed = false;
            }

            // ========================================================================
            // 2. 对于有向图层级节点的重算（包含复杂的数学 Solver 和 Plotter），完全发挥多线程潜力！
            // 直接调用 oneTBB 自动分区并行器。不设任何人为颗粒度限制，
            // 彻底交由 TBB 动态工作窃取（Work-Stealing）调度器压榨所有 CPU 物理与逻辑核心。
            // ========================================================================
            for (auto& level_nodes : cached_levels) {
                tbb::parallel_for(size_t(0), level_nodes.size(), [&](size_t i) {
                    EvaluatePushNode(level_nodes[i]);
                });
            }

            // ========================================================================
            // 3. 【核心修正】：对于极其简单的清除标记操作（仅位运算），一律不用 TBB！
            // 回归最干净、最具空间局部性的顺序单线程循环，允许编译器进行极致的 SIMD 向量化优化
            // ========================================================================
            for (size_t i = 0; i < node_pool.size(); ++i) {
                node_pool[i].clear_mask(NodeMask::RECALCULATED);
            }
        }

        void EvaluatePushNode(Object<T>* node) noexcept {
            bool parent_dirty = false;
            for (const auto* parent : node->parents) {
                if (parent && parent->has_mask(NodeMask::RECALCULATED)) {
                    parent_dirty = true;
                    break;
                }
            }

            if (node->has_mask(NodeMask::DIRTY) || parent_dirty) {
                if (node->vptr) {
                    if (node->vptr->solver) {
                        node->vptr->solver(*this, *node);
                    }
                    if (node->vptr->plotter) {
                        node->result_points_3d.clear();
                        node->result_strips.clear();
                        node->vptr->plotter(*this, *node);
                    }
                }
                node->clear_mask(NodeMask::DIRTY);
                node->set_mask(NodeMask::RECALCULATED);
            }
        }

        /**
         * @brief 极致安全的堆递归 JIT 逆向惰性求值核心 (100% 免疫 Stack Overflow)
         * @return true 代表该节点或其任一上游祖先在本次查询中发生了重新计算
         */
        bool EvaluateModelJIT(Object<T>* root_model) {
            if (!root_model || !root_model->active) return false;

            // 堆递归专用状态帧，彻底摆脱系统调用栈依赖
            struct StackFrame {
                Object<T>* node;
                size_t parent_index;
            };

            std::vector<StackFrame> dfs_stack;
            std::vector<Object<T>*> ordered_nodes;

            // 预分配内存，防止运行期动态扩容带来性能抖动
            dfs_stack.reserve(128);
            ordered_nodes.reserve(128);

            // 1. 第一阶段：迭代式拓扑排序 (DFS Post-Order)
            dfs_stack.push_back({root_model, 0});
            root_model->set_mask(NodeMask::VISITED); // 替代 std::unordered_set 的高开销方案

            while (!dfs_stack.empty()) {
                auto& frame = dfs_stack.back();
                Object<T>* curr = frame.node;

                if (frame.parent_index < curr->parents.size()) {
                    const Object<T>* parent_const = curr->parents[frame.parent_index];
                    frame.parent_index++; // 步进，指向下一个父节点

                    Object<T>* parent = const_cast<Object<T>*>(parent_const);
                    if (parent && parent->active && !parent->has_mask(NodeMask::VISITED)) {
                        parent->set_mask(NodeMask::VISITED);
                        dfs_stack.push_back({parent, 0});
                    }
                } else {
                    // 所有父依赖均已探测完毕，当前节点安全出栈并存入拓扑队列
                    ordered_nodes.push_back(curr);
                    dfs_stack.pop_back();
                }
            }

            // 2. 第二阶段：自底向上顺序重算
            bool root_recalculated = false;

            for (auto* node : ordered_nodes) {
                bool parent_recalculated = false;

                // 极速脏检查：是否有父节点在本次执行中被重新计算过
                for (const auto* parent : node->parents) {
                    if (parent && parent->has_mask(NodeMask::RECALCULATED)) {
                        parent_recalculated = true;
                        break;
                    }
                }

                // 脏重算判定：
                //   - 自身被直接标记为脏 (Has DIRTY mask)
                //   - 在本次拉取中，其任意上游祖先发生过重算 (parent_recalculated == true)
                if (node->has_mask(NodeMask::DIRTY) || parent_recalculated) {
                    if (node->vptr) {
                        if (node->vptr->solver) {
                            node->vptr->solver(*this, *node);
                        }
                        if (node->vptr->plotter) {
                            node->result_points_3d.clear();
                            node->result_strips.clear();
                            node->vptr->plotter(*this, *node);
                        }
                    }
                    node->clear_mask(NodeMask::DIRTY);
                    node->set_mask(NodeMask::RECALCULATED); // 标记当前节点重算过

                    if (node == root_model) {
                        root_recalculated = true;
                    }
                }
            }

            // 3. 【自愈】：清理物理自检掩码
            // 这里的位清除操作极其轻量，同样回归最纯粹的顺序循环，允许编译器自动对其进行 SIMD 向量化
            for (auto* node : ordered_nodes) {
                node->clear_mask(NodeMask::VISITED | NodeMask::RECALCULATED);
            }

            return root_recalculated;
        }
    };

} // namespace StuCanvas