#pragma once
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include "../types/point.hpp"         // 保持原始 Point3D, Point2D 无 UV 格式
#include "../types/segment_strip.hpp"  // 包含 SegmentStrip3D
#include "../utils/block_deque.hpp"
#include "stucanvas/types/cpu/cpu_types.hpp"
#include "types.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Object;
    template <typename T>
    struct ObjectGraph;

    enum class NodeMask : uint64_t
    {
        NONE = 0,
        DIRTY = 1ULL << 0, // 标记该节点本身被直接修改（数值或样式）
        VISITED = 1ULL << 1, // 用于堆递归中 DFS 的访问标记
        RECALCULATED = 1ULL << 2, // 用于自底向上重算时的状态传导标记
        QUERIED = 1ULL << 3
    };


    // 虚表定义
    template <typename T>
    struct ObjectVTable
    {
        void (*solver)(ObjectGraph<T>&, Object<T>&) = nullptr;
        void (*plotter)(ObjectGraph<T>&, Object<T>&) = nullptr;
    };

    template <typename T>
    struct Object
    {
        NodeType type = NodeType::UNKNOWN;
        mutable uint64_t mask = 0;
        std::string name;
        uint64_t id{};

        utils::BlockDeque<const Object*, 4> parents;
        utils::BlockDeque<const Object*, 16> children;

        union
        {
            struct
            {
                T x, y;
            } point_2d;

            struct
            {
                T x0, y0, x1, y1;
            } line_2d;

            struct
            {
                T a, b, c, d;
            } plane_3d;
        } data;

        const ObjectVTable<T>* vptr = nullptr;
        ObjectGraph<T>* graph = nullptr; // 反向指针

        // =========================================================================
        // 💡 【五法则实现】：保障有向图拓扑安全的极致工程设计
        // =========================================================================

        // ① 默认构造函数
        Object() noexcept = default;

        // ② 析构函数：由于 std::string name, 以及 BlockDeque 内部自己拥有完善的 RAII 释放，
        // 默认析构函数会自动、安全地释放它们，无需我们在此手动释放内存。
        ~Object() noexcept = default;

        // ③ 极其关键：显式删除拷贝构造与拷贝赋值，防止图拓扑结构发生不可逆的野指针崩塌
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        // ④ 移动构造：接管所有数据，并对整个计算图进行“指针打补丁”
        Object(Object&& other) noexcept
            : type(other.type),
              mask(other.mask),
              name(std::move(other.name)),
              id(other.id),
              parents(std::move(other.parents)),
              children(std::move(other.children)),
              data(other.data),
              vptr(other.vptr),
              graph(other.graph)
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
        Object& operator=(Object&& other) noexcept
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
        // 💡 物理指针打补丁：让所有与旧地址相连的节点，无缝指向新地址
        void patch_connections(Object* old_addr, Object* new_addr) noexcept
        {
            // 让所有父节点中指向 old_addr 的子指针，全部改写为指向 new_addr
            for (size_t i = 0; i < parents.size(); ++i)
            {
                Object* parent = parents[i];
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
                Object* child = children[i];
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

        // 💡 安全解耦：从当前的拓扑网路中彻底蒸发自己，不留任何悬空指针
        void disconnect_self() noexcept
        {
            for (size_t i = 0; i < parents.size(); ++i)
            {
                Object* parent = parents[i];
                if (parent)
                {
                    // 利用 C++20 的 std::erase 极速从父节点的子队列中抹去自己
                    std::erase(parent->children, this);
                }
            }
            for (size_t i = 0; i < children.size(); ++i)
            {
                Object* child = children[i];
                if (child)
                {
                    std::erase(child->parents, this);
                }
            }
        }

        void NotifyDirty() const noexcept
        {
            set_mask(NodeMask::DIRTY);
        }
    };
}
