// stucanvas/object/graph.ipp
#pragma once
#include "graph.hpp"

namespace StuCanvas
{
    // ========================================================================
    // 0. 大一统内联脏化辅助函数（GPU 级极速热路径）
    // ========================================================================
    template <typename T>
    inline void SObjectGraph<T>::markDirty(SObject<T>* node) noexcept
    {
        // 极致去重：仅在节点尚未变脏时，标记并推入脏列表
        if (!node->has_mask(NodeMask::DIRTY))
        {
            node->set_mask(NodeMask::DIRTY);
            dirty_list.push_back(node);
        }
    }

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

        // 1) 从旧父节点的 children 列表中剔除当前节点
        for (const auto* old_parent : node->parents)
        {
            if (old_parent)
            {
                auto* mutable_parent = const_cast<SObject<T>*>(old_parent);
                auto& children = mutable_parent->children;
                children.erase(std::remove(children.begin(), children.end(), node), children.end());
            }
        }

        // 2) 清空原有的 parents 关系
        node->parents.clear();

        // 3) 建立与新父节点的新双向连线
        for (const auto* new_parent : new_parents)
        {
            if (new_parent)
            {
                node->parents.push_back(new_parent);
                const_cast<SObject<T>*>(new_parent)->children.push_back(node);
            }
        }

        // 4) 触发脏标记
        markDirty(node);

        // 5) 由于图谱拓扑改变，标记为真，在下次 Compute() 时重新运行 Kahn 算法编译层级
        topology_changed = true;
    }
} // namespace StuCanvas