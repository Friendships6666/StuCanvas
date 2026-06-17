// stucanvas/sobject/layout.hpp
#pragma once

#include <vector>
#include <functional>
#include <algorithm>
#include <limits>
#include "eigen3/Eigen/Dense"

#include "../utils/block_deque.hpp"
#include "instance.hpp"
#include "graph.hpp"

namespace StuCanvas
{
    // ========================================================================
    // InstanceChannel：实例属性通道枚举
    // ========================================================================
    enum class InstanceChannel : uint8_t
    {
        PositionX,
        PositionY,
        PositionZ,
        RotationX, // 四元数分量 x
        RotationY, // 四元数分量 y
        RotationZ, // 四元数分量 z
        RotationW, // 四元数分量 w
        ScaleX,
        ScaleY,
        ScaleZ
    };

    template <typename T>
    struct InstanceLayoutSolver;

    // ========================================================================
    // SObjectInstanceLayout：实例层级排版计算图 (DAG 2)
    // ========================================================================
    template <typename T>
    class SObjectInstanceLayout
    {
    public:
        using value_type = T;

        // 1. 物理寻址安全大池：确保 Instance 节点移动与扩容时的绝对指针稳定性
        utils::BlockDeque<SObjectInstance<T>, 256> instance_pool;

        // 2. 解算器绑定：记录实例层面所有的排版解算规则
        std::vector<InstanceLayoutSolver<T>> solvers;

        // 3. 编译后的解算顺序（拓扑排序输出）
        std::vector<InstanceLayoutSolver<T>> compiled_solvers;
        bool solvers_changed = true;

        SObjectInstanceLayout() noexcept = default;
        ~SObjectInstanceLayout() noexcept = default;

        // 显式禁止拷贝，防范拓扑指针失效
        SObjectInstanceLayout(const SObjectInstanceLayout&) = delete;
        SObjectInstanceLayout& operator=(const SObjectInstanceLayout&) = delete;

        SObjectInstanceLayout(SObjectInstanceLayout&&) noexcept = default;
        SObjectInstanceLayout& operator=(SObjectInstanceLayout&&) noexcept = default;

        // =========================================================================
        // 核心接口 1：创建空间实例 (SObjectInstance)
        // =========================================================================

        SObjectInstance<T>* createInstance(const SObjectFamily<T>* family)
        {
            if (!family) return nullptr;

            auto& inst = instance_pool.emplace_back();
            inst.source_family = family;
            return &inst;
        }

        SObjectInstance<T>* createInstance(const SObjectFamily<T>* family, const std::vector<SObjectInstance<T>*>& parents)
        {
            if (!family) return nullptr;

            auto& inst = instance_pool.emplace_back();
            inst.source_family = family;

            for (auto* parent : parents)
            {
                if (parent)
                {
                    inst.parents.push_back(parent);
                    parent->children.push_back(&inst);
                }
            }
            return &inst;
        }

        // =========================================================================
        // 核心接口 2：直接修改物理位置、旋转、缩放通道 (数值修改)
        // =========================================================================

        void modifyInstanceTransform(
            SObjectInstance<T>* instance,
            const Eigen::Matrix<T, 3, 1>& position,
            const Eigen::Quaternion<T>& rotation = Eigen::Quaternion<T>::Identity(),
            const Eigen::Matrix<T, 3, 1>& scale = Eigen::Matrix<T, 3, 1>::Ones()) noexcept
        {
            if (!instance) return;
            instance->world_position = position;
            instance->world_rotation = rotation;
            instance->world_scales = scale;
        }

        void modifyInstancePosition(SObjectInstance<T>* instance, T x, T y, T z) noexcept
        {
            if (!instance) return;
            instance->world_position = Eigen::Matrix<T, 3, 1>(x, y, z);
        }

        // =========================================================================
        // 核心接口 3：排版绑定解算器注册与拓扑解算 (Topological DAG Solve)
        // =========================================================================

        /**
         * @brief 绑定一个排版解算器，控制特定实例的特定属性通道
         * @param target_instance 被控制的目标实例
         * @param target_channel 被控制的属性通道
         * @param solver 自定义排版计算函数
         * @param dependencies 该计算所依赖的其它源实例列表（用于构建排版 DAG 依赖）
         */
        void bindInstanceSolver(
            SObjectInstance<T>* target_instance,
            InstanceChannel target_channel,
            std::function<T(const SObjectInstanceLayout<T>&)> solver,
            std::vector<const SObjectInstance<T>*> dependencies = {})
        {
            if (!target_instance || !solver) return;

            solvers.push_back({target_instance, target_channel, std::move(solver), std::move(dependencies)});
            solvers_changed = true;
        }

        /**
         * @brief 将所有排版约束解算器按依赖关系编译为拓扑排序序列
         */
        void CompileLayoutDAG()
        {
            size_t n = solvers.size();
            compiled_solvers.clear();
            compiled_solvers.reserve(n);

            std::vector<size_t> in_degrees(n, 0);
            std::vector<std::vector<size_t>> adj(n);

            // 构建解算器之间的拓扑依赖关系：
            // 如果解算器 i 依赖的实例是解算器 j 控制的目标实例，则建立一条 j -> i 的依赖有向边
            for (size_t i = 0; i < n; ++i)
            {
                const auto& i_deps = solvers[i].dependencies;
                for (size_t j = 0; j < n; ++j)
                {
                    if (i == j) continue;
                    if (std::find(i_deps.begin(), i_deps.end(), solvers[j].target_instance) != i_deps.end())
                    {
                        adj[j].push_back(i);
                        in_degrees[i]++;
                    }
                }
            }

            // 使用 Kahn 算法进行拓扑排序
            std::vector<size_t> queue;
            queue.reserve(n);
            for (size_t i = 0; i < n; ++i)
            {
                if (in_degrees[i] == 0)
                {
                    queue.push_back(i);
                }
            }

            size_t head = 0;
            while (head < queue.size())
            {
                size_t curr = queue[head++];
                compiled_solvers.push_back(solvers[curr]);

                for (size_t next : adj[curr])
                {
                    if (--in_degrees[next] == 0)
                    {
                        queue.push_back(next);
                    }
                }
            }

            // 如果存在循环依赖导致排序不完全，回退到原始添加顺序，防止解算节点丢失
            if (compiled_solvers.size() < n)
            {
                compiled_solvers = solvers;
            }

            solvers_changed = false;
        }

        /**
         * @brief 执行排版 DAG。按照编译后的拓扑顺序依次解算每个实例的物理通道数值
         */
        void SolveLayout()
        {
            if (solvers_changed)
            {
                CompileLayoutDAG();
            }

            // 1. 严格按照拓扑顺序依次解算
            for (auto& solver_binding : compiled_solvers)
            {
                if (!solver_binding.target_instance || !solver_binding.solver) continue;

                // 运行解算函数
                T computed_val = solver_binding.solver(*this);

                // 2. 将数值写入对应的 Instance 通道
                writeChannelValue(solver_binding.target_instance, solver_binding.target_channel, computed_val);
            }

            // 3. 扫尾工作：对被修改过旋转分量的实例重新执行四元数归一化，防止多次解算引入浮点数精度漂移
            for (auto& solver_binding : compiled_solvers)
            {
                if (solver_binding.target_instance &&
                    (solver_binding.target_channel == InstanceChannel::RotationX ||
                     solver_binding.target_channel == InstanceChannel::RotationY ||
                     solver_binding.target_channel == InstanceChannel::RotationZ ||
                     solver_binding.target_channel == InstanceChannel::RotationW))
                {
                    solver_binding.target_instance->world_rotation.normalize();
                }
            }
        }

    private:
        /**
         * @brief 通道物理修改辅助函数
         */
        void writeChannelValue(SObjectInstance<T>* inst, InstanceChannel channel, T value) noexcept
        {
            switch (channel)
            {
                case InstanceChannel::PositionX: inst->world_position(0) = value; break;
                case InstanceChannel::PositionY: inst->world_position(1) = value; break;
                case InstanceChannel::PositionZ: inst->world_position(2) = value; break;
                case InstanceChannel::RotationX: inst->world_rotation.coeffs()(0) = value; break;
                case InstanceChannel::RotationY: inst->world_rotation.coeffs()(1) = value; break;
                case InstanceChannel::RotationZ: inst->world_rotation.coeffs()(2) = value; break;
                case InstanceChannel::RotationW: inst->world_rotation.coeffs()(3) = value; break;
                case InstanceChannel::ScaleX:    inst->world_scales(0) = value; break;
                case InstanceChannel::ScaleY:    inst->world_scales(1) = value; break;
                case InstanceChannel::ScaleZ:    inst->world_scales(2) = value; break;
            }
        }

    public:
        // =========================================================================
        // 4. 内置高能排版解算器工厂方法：自动考虑尺度与任意自转/偏置的精确空间解算
        // =========================================================================

        /**
         * @brief 边界对齐/贴合解算器 (AABB Alignment Solver)
         * 让目标实例 target_inst 的指定边界贴合到源实例 source_inst 的指定边界上（可带 Gap 间距）
         */
        static std::function<T(const SObjectInstanceLayout<T>&)> CreateAlignmentSolver(
            SObjectInstance<T>* target_inst,
            const SObjectInstance<T>* source_inst,
            AlignBoundary target_boundary,
            AlignBoundary source_boundary,
            int axis = 0, // 0: X, 1: Y, 2: Z
            T gap = 0)
        {
            return [target_inst, source_inst, target_boundary, source_boundary, axis, gap](const SObjectInstanceLayout<T>&) -> T
            {
                if (!target_inst || !source_inst) return static_cast<T>(0);

                // 1. 获取源实例的世界包围盒边界值
                AABB3D<T> src_box = source_inst->GetWorldAABB();
                T src_val = 0;
                if (source_boundary == AlignBoundary::Min) src_val = src_box.min(axis);
                else if (source_boundary == AlignBoundary::Max) src_val = src_box.max(axis);
                else src_val = (src_box.min(axis) + src_box.max(axis)) * static_cast<T>(0.5);

                T desired_val = src_val + gap;

                // 2. 获取目标实例当前的边界与其世界位置 (Pivot/Center) 之间的相对相对相对偏移量
                // 这样即使目标实例带有自转和复杂的 local AABB 偏置，也能被绝对精确地贴合
                AABB3D<T> tgt_box = target_inst->GetWorldAABB();
                T current_boundary_val = 0;
                if (target_boundary == AlignBoundary::Min) current_boundary_val = tgt_box.min(axis);
                else if (target_boundary == AlignBoundary::Max) current_boundary_val = tgt_box.max(axis);
                else current_boundary_val = (tgt_box.min(axis) + tgt_box.max(axis)) * static_cast<T>(0.5);

                T pivot_offset = current_boundary_val - target_inst->world_position(axis);

                // 3. 计算为了达到该边界，世界坐标应变更为多少
                return desired_val - pivot_offset;
            };
        }

        /**
         * @brief 居中解算器 (AABB Centering Solver)
         * 将目标实例 target_inst 居中放置在 source_inst_a 和 source_inst_b 两个实例的中间
         */
        static std::function<T(const SObjectInstanceLayout<T>&)> CreateCenteringSolver(
            SObjectInstance<T>* target_inst,
            const SObjectInstance<T>* source_inst_a,
            const SObjectInstance<T>* source_inst_b,
            int axis = 0)
        {
            return [target_inst, source_inst_a, source_inst_b, axis](const SObjectInstanceLayout<T>&) -> T
            {
                if (!target_inst || !source_inst_a || !source_inst_b) return static_cast<T>(0);

                AABB3D<T> box_a = source_inst_a->GetWorldAABB();
                AABB3D<T> box_b = source_inst_b->GetWorldAABB();

                T center_a = (box_a.min(axis) + box_a.max(axis)) * static_cast<T>(0.5);
                T center_b = (box_b.min(axis) + box_b.max(axis)) * static_cast<T>(0.5);
                T target_center = (center_a + center_b) * static_cast<T>(0.5);

                // 消除偏置，精确居中
                AABB3D<T> tgt_box = target_inst->GetWorldAABB();
                T current_center = (tgt_box.min(axis) + tgt_box.max(axis)) * static_cast<T>(0.5);
                T pivot_offset = current_center - target_inst->world_position(axis);

                return target_center - pivot_offset;
            };
        }
    };

    // ========================================================================
    // InstanceLayoutSolver：实例属性通道排版约束绑定结构
    // ========================================================================
    template <typename T>
    struct InstanceLayoutSolver
    {
        SObjectInstance<T>* target_instance = nullptr;                   // 被控制的目标实例
        InstanceChannel target_channel = InstanceChannel::PositionX;     // 控制的特定属性通道
        std::function<T(const SObjectInstanceLayout<T>&)> solver = nullptr; // 排版解算核心
        std::vector<const SObjectInstance<T>*> dependencies;             // 该约束依赖的源实例集合
    };
} // namespace StuCanvas