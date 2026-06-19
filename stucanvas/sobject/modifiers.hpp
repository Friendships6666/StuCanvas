// stucanvas/object/graph.ipp
#pragma once
#include "graph.hpp"

namespace StuCanvas
{
    // ========================================================================
    // 1. 裸金属级数值修改函数（热路径：仅允许修改数值，绝无任何多余判断）
    // ========================================================================

    template <typename T>
    void SObjectGraph<T>::modifyPoint2D(const SObject<T>* model_ptr, T new_x, T new_y)
    {
        auto* node = const_cast<SObject<T>*>(model_ptr);

        // 直接强行物理覆写，不进行任何冗余的浮点数大小对比
        node->data.point_2d.x = new_x;
        node->data.point_2d.y = new_y;

        // 瞬间脏化并推入脏列表
        markDirty(node);
    }

    template <typename T>
    void SObjectGraph<T>::modifyScalar(const SObject<T>* model_ptr, T new_value)
    {
        auto* node = const_cast<SObject<T>*>(model_ptr);

        node->data.scalar.value = new_value;

        markDirty(node);
    }

    // ========================================================================
    // 2. 独立属性修改器（冷路径：改名字、改关系，不污染高频数值修改）
    // ========================================================================

    template <typename T>
    void SObjectGraph<T>::modifyCircle2DRadius(const SObject<T>* model_ptr, T new_radius)
    {
        auto* node = const_cast<SObject<T>*>(model_ptr);
        node->data.circle_2d.r = new_radius;
        markDirty(node);
    }

    // 极致优化：改名字只涉及描述性元数据，不影响任何几何代数计算，因此无需脏化图谱和重解算！
    template <typename T>
    void SObjectGraph<T>::modifyName(const SObject<T>* model_ptr, std::string_view new_name)
    {
        if (!model_ptr) return;
        auto* node = const_cast<SObject<T>*>(model_ptr);
        node->name = new_name;
    }

    template <typename T>
    void SObjectGraph<T>::modifyParents(const SObject<T>* model_ptr, const std::vector<const SObject<T>*>& new_parents)
    {
        if (!model_ptr) return;
        auto* node = const_cast<SObject<T>*>(model_ptr);

        // 💡 掩码对映射：统一定义所有离散化资源的依赖对
        struct MaskPair
        {
            NodeMask required;
            NodeMask generator;
        };
        static constexpr MaskPair kDiscreteMaskPairs[] = {
            {NodeMask::TRIANGLES_REQUIRED, NodeMask::TRIANGLES_GENERATOR},
            {NodeMask::STRIPS_REQUIRED, NodeMask::STRIPS_GENERATOR},
            // 未来如果增加 POINTS，直接加一行即可：
            {NodeMask::POINTS_REQUIRED, NodeMask::POINTS_GENERATOR}
        };

        // 1) 从旧父节点的 children 列表中剔除当前节点，并统一回收旧父级不再需要的掩码
        for (const auto* old_parent : node->parents)
        {
            if (old_parent)
            {
                auto* mutable_parent = const_cast<SObject<T>*>(old_parent);
                auto& children = mutable_parent->children;

                // 剔除当前节点
                children.erase(std::remove(children.begin(), children.end(), node), children.end());

                // 💡 复用：遍历所有资源类型，检查并回收旧父级的 Generator 标记
                for (const auto& mask_pair : kDiscreteMaskPairs)
                {
                    bool any_child_needs = false;
                    for (const auto* remaining_child : children)
                    {
                        if (remaining_child && remaining_child->has_mask(mask_pair.required))
                        {
                            any_child_needs = true;
                            break;
                        }
                    }

                    // 如果没有任何剩余子节点需要该资源，清除该父级的对应 Generator
                    if (!any_child_needs)
                    {
                        mutable_parent->clear_mask(mask_pair.generator);
                    }
                }
            }
        }

        // 2) 清空原有的 parents 关系
        node->parents.clear();

        // 3) 建立与新父节点的新双向连线，并统一传递所需的 Generator 掩码
        for (const auto* new_parent : new_parents)
        {
            if (new_parent)
            {
                node->parents.push_back(new_parent);
                auto* mutable_parent = const_cast<SObject<T>*>(new_parent);
                mutable_parent->children.push_back(node);

                // 💡 复用：遍历所有资源类型，若当前节点有需求，则递送给新父级对应的 Generator
                for (const auto& mask_pair : kDiscreteMaskPairs)
                {
                    if (node->has_mask(mask_pair.required))
                    {
                        mutable_parent->set_mask(mask_pair.generator);
                    }
                }
            }
        }

        // 4) 触发脏标记
        markDirty(node);

        // 5) 由于图谱拓扑改变，在下次 Compute() 时重新运行 Kahn 算法编译层级
        topology_changed = true;
    }
} // namespace StuCanvas
