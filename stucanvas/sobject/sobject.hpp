#pragma once
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include "../types/point.hpp"         // 保持原始 Point3D, Point2D 无 UV 格式
#include "../types/segment_strip.hpp"  // 包含 SegmentStrip3D
#include "../utils/block_deque.hpp"
#include "../types/cpu/cpu_types.hpp"
#include "sobject_types.hpp"
#include "sobject_data.hpp"
#include "block_deque.hpp"
namespace StuCanvas
{

    template <typename T>
    struct SObjectGraph;

    enum class NodeMask : uint64_t
    {
        NONE = 0,
        DIRTY = 1ULL << 0, // 标记该节点本身被直接修改（数值或样式）
        VISITED = 1ULL << 1, // 用于堆递归中 DFS 的访问标记
        RECALCULATED = 1ULL << 2, // 用于自底向上重算时的状态传导标记
        QUERIED = 1ULL << 3,
        TRIANGLES_REQUIRED = 1ULL << 4,
        TRIANGLES_GENERATOR = 1ULL << 5,
        STRIPS_REQUIRED = 1ULL << 6,
        STRIPS_GENERATOR = 1ULL << 7,
        POINTS_REQUIRED = 1ULL << 8,
        POINTS_GENERATOR = 1ULL << 9,
    };



    template <typename T>
    struct SObjectInstance;



    template <typename T>
    struct SObject
    {
        NodeType type = NodeType::UNKNOWN;
        mutable uint64_t mask = 0;
        std::string name;
        uint64_t id{};



        utils::BlockDeque<const SObject*, 4> parents;
        utils::BlockDeque<const SObject*, 16> children;
        T discretization_step_points{};
        union
        {
            T discretization_step_strips{};
            T discretization_step_triangles;
        } discretization_step_union;


        utils::BlockDeque<const SObjectInstance<T>*, 16> instances;
        SObjectData<T> data;

        const SObjectVTable<T>* vptr = nullptr;

        SObjectGraph<T>* graph = nullptr; // 反向指针



        SObject() noexcept = default;

        ~SObject() noexcept = default;

        // ③ 极其关键：显式删除拷贝构造与拷贝赋值，防止图拓扑结构发生不可逆的野指针崩塌
        SObject(const SObject&) = delete;
        SObject& operator=(const SObject&) = delete;

        // ④ 移动构造：接管所有数据，并对整个计算图进行“指针打补丁”
        SObject(SObject&& other) noexcept
            : type(other.type),
              mask(other.mask),
              name(std::move(other.name)),
              id(other.id),
              parents(std::move(other.parents)),
              children(std::move(other.children)),
              data(other.data),
              vptr(other.vptr),
              graph(other.graph),
              discretization_step_points(other.discretization_step_points),
              discretization_step_union(other.discretization_step_union)
        {
            // 因为我们的物理内存地址变了（从 &other 变成了 this），
            // 必须让所有父子节点将指向 &other 的指针重新修正为指向 this！
            patch_connections(&other, this);

            // 将原临时对象置于安全、无害的默认状态
            other.type = NodeType::UNKNOWN;
            other.mask = 0;
            other.id = 0;
            other.vptr = nullptr;
            other.graph = nullptr;
        }

        // ⑤ 移动赋值运算符
        SObject& operator=(SObject&& other) noexcept
        {
            if (this != &other)
            {
                // 首先安全断开自己原有的所有父子连接（如果有的话），防止产生悬空引用
                disconnect_self();

                type = other.type;
                mask = other.mask;
                name = std::move(other.name);
                id = other.id;
                parents = std::move(other.parents);
                children = std::move(other.children);
                data = other.data;
                vptr = other.vptr;
                graph = other.graph;
                discretization_step_points = other.discretization_step_points;
                discretization_step_union = other.discretization_step_union;

                // 重新为计算图打上物理指针补丁
                patch_connections(&other, this);

                other.type = NodeType::UNKNOWN;
                other.mask = 0;
                other.id = 0;
                other.vptr = nullptr;
                other.graph = nullptr;
            }
            return *this;
        }

        // =========================================================================
        // 拓扑辅助接口
        // =========================================================================

        void set_mask(NodeMask m) const noexcept { mask |= static_cast<uint64_t>(m); }
        void clear_mask(NodeMask m) const noexcept { mask &= ~static_cast<uint64_t>(m); }
        [[nodiscard]] bool has_mask(NodeMask m) const noexcept { return (mask & static_cast<uint64_t>(m)) != 0; }

    private:
        void patch_connections(SObject* old_addr, SObject* new_addr) noexcept
        {
            // 让所有父节点中指向 old_addr 的子指针，全部改写为指向 new_addr
            for (size_t i = 0; i < parents.size(); ++i)
            {
                SObject* parent = parents[i];
                if (parent)
                {
                    for (size_t j = 0; j < parent->children.size(); ++j)
                    {
                        if (parent->children[j] == old_addr)
                        {
                            parent->children[j] = new_addr;
                        }
                    }
                }
            }
            // 让所有子节点中指向 old_addr 的父指针，全部改写为指向 new_addr
            for (size_t i = 0; i < children.size(); ++i)
            {
                SObject* child = children[i];
                if (child)
                {
                    for (size_t j = 0; j < child->parents.size(); ++j)
                    {
                        if (child->parents[j] == old_addr)
                        {
                            child->parents[j] = new_addr;
                        }
                    }
                }
            }
        }

        void disconnect_self() noexcept
        {
            for (size_t i = 0; i < parents.size(); ++i)
            {
                const SObject* parent = parents[i];
                if (parent)
                {
                    // 物理强转并调用我们的无序快速抹除
                    const_cast<SObject*>(parent)->children.erase_unordered(this);
                }
            }
            for (size_t i = 0; i < children.size(); ++i)
            {
                const SObject* child = children[i];
                if (child)
                {
                    const_cast<SObject*>(child)->parents.erase_unordered(this);
                }
            }
        }

    };
}
