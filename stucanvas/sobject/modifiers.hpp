#pragma once
#include "graph.hpp"

namespace StuCanvas
{
     template <typename T>
    void SObjectGraph<T>::modifyPoint2D(const SObject<T>* model_ptr, T new_x, T new_y, const ModifyPoint2DInfo<T>& info)
    {
        if (!model_ptr) return;
        auto* node = const_cast<SObject<T>*>(model_ptr);

        // A. 数值修改判定
        if (node->data.point_2d.x != new_x || node->data.point_2d.y != new_y)
        {
            node->data.point_2d.x = new_x;
            node->data.point_2d.y = new_y;
            if (!node->has_mask(NodeMask::DIRTY))
            {
                node->set_mask(NodeMask::DIRTY);
                dirty_list.push_back(node);
            }
            curr_frame_stats.dirty_count++;
        }

        // B. 可选：修改名称
        if (!info.name.empty())
        {
            node->name = info.name;
        }

        // C. 可选：重新连线与关系变动
        if (!info.parents.empty())
        {
            modifyParents(model_ptr, info.parents);
        }
    }

    template <typename T>
    void SObjectGraph<T>::modifyParents(const SObject<T>* model_ptr, const std::vector<const SObject<T>*>& new_parents)
    {
        if (!model_ptr) return;
        auto* node = const_cast<SObject<T>*>(model_ptr);

        // 1) 安全拆除：从旧父节点的 children 列表中剔除当前节点
        for (const auto* old_parent : node->parents)
        {
            if (old_parent)
            {
                auto* mutable_parent = const_cast<SObject<T>*>(old_parent);
                auto& children = mutable_parent->children;
                children.erase(std::remove(children.begin(), children.end(), node), children.end());
            }
        }

        // 2) 清空当前节点原有的 parents 槽位
        node->parents.clear();

        // 3) 重新组装：建立与新父节点的双向依存连线
        for (const auto* new_parent : new_parents)
        {
            if (new_parent)
            {
                node->parents.push_back(new_parent);
                const_cast<SObject<T>*>(new_parent)->children.push_back(node);
            }
        }

        // 4) 触发脏标记
        if (!node->has_mask(NodeMask::DIRTY))
        {
            node->set_mask(NodeMask::DIRTY);
            dirty_list.push_back(node);
        }

        // 5) 核心：由于图谱拓扑发生变动，必须标记为真，触发下一次 Compute() 时的 Kahn 全局层级重编译
        topology_changed = true;
        curr_frame_stats.dirty_count++;
    }
}