#pragma once

#include "graph.hpp"

namespace StuCanvas
{
     template <typename T>
    SObject<T>* SObjectGraph<T>::AllocateModel(NodeType type, std::string_view name,bool is_dirty)
    {
        auto& node = node_pool.emplace_back();
        node.type = type;
        node.name = name;
        node.graph = this;

        if (is_dirty)
        {
            node.set_mask(NodeMask::DIRTY);
            dirty_list.push_back(&node);
        }

        topology_changed = true;
        return &node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createFreePoint2D(T x, T y, const Point2DCreateInfo<T>& info)
    {
        SObject<T>* node = AllocateModel(NodeType::POINT_2D_FREE, info.name);
        node->data.point_2d.x = x;
        node->data.point_2d.y = y;
        node->vptr = &Point2DFree_VTable<T>;

        // 建立双向依赖连线
        for (const auto* parent : info.parents)
        {
            if (parent)
            {
                node->parents.push_back(parent);
                const_cast<SObject<T>*>(parent)->children.push_back(node);
            }
        }
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createFreePoint3D(T x, T y, T z, const Point3DCreateInfo<T>& info)
    {
        SObject<T>* node = AllocateModel(NodeType::POINT_2D_FREE, info.name);
        node->data.point_3d.x = x;
        node->data.point_3d.y = y;
        node->data.point_3d.z = z;
        node->vptr = &Point3DFree_VTable<T>;

        // 建立双向依赖连线
        for (const auto* parent : info.parents)
        {
            if (parent)
            {
                node->parents.push_back(parent);
                const_cast<SObject<T>*>(parent)->children.push_back(node);
            }
        }
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createSegment2D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_2D_SEGMENT, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line2DSegment_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createStraightLine2D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_2D_STRAIGHT, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line2DStraight_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createRay2D(const SObject<T>* p1, const SObject<T>* p2, std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_2D_RAY, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line2DRay_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createSegment3D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_3D_SEGMENT, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line3DSegment_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createStraightLine3D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_3D_STRAIGHT, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line3DStraight_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createRay3D(const SObject<T>* p1, const SObject<T>* p2, std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::LINE_3D_RAY, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Line3DRay_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createPlane3D(const SObject<T>* p1, const SObject<T>* p2, const SObject<T>* p3,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::PLANE_3D, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->parents.push_back(p3);
        node->vptr = &Plane3D_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        const_cast<SObject<T>*>(p3)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createMidPoint2D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::POINT_2D_MID, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Point2DMid_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }

    template <typename T>
    const SObject<T>* SObjectGraph<T>::createMidPoint3D(const SObject<T>* p1, const SObject<T>* p2,
        std::string_view name)
    {
        SObject<T>* node = AllocateModel(NodeType::POINT_3D_MID, name);
        node->parents.push_back(p1);
        node->parents.push_back(p2);
        node->vptr = &Point3DMid_VTable<T>;

        const_cast<SObject<T>*>(p1)->children.push_back(node);
        const_cast<SObject<T>*>(p2)->children.push_back(node);
        return node;
    }


    template <typename T>
    /**
             * @brief 创建一个纯代数标量节点 (无父节点，仅作为计算源)
             * @param value 初始标量值
             * @param info C++20 引导参数包 (仅支持改名)
             */
            const SObject<T>* SObjectGraph<T>::createScalar(T value,std::string_view name)
     {
         // 物理分配（node_pool 分配会将节点默认置脏，从而加入脏列表进行首帧解算）
         SObject<T>* node = AllocateModel(NodeType::SCALAR, name,false);
         node->data.scalar.value = value;

         // 标量无父节点，因此 node->parents 保持默认空
         return node;
     }


}