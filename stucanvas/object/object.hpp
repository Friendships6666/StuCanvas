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
namespace StuCanvas {

    template <typename T> struct Object;
    template <typename T> struct ObjectGraph;

    enum class NodeMask : uint64_t {
        NONE = 0,
        DIRTY = 1ULL << 0, // 标记该节点本身被直接修改（数值或样式）
        VISITED      = 1ULL << 1, // 用于堆递归中 DFS 的访问标记
        RECALCULATED = 1ULL << 2, // 用于自底向上重算时的状态传导标记
        QUERIED = 1ULL << 3
    };



    // 超大视觉属性结构体
    template <typename T>
    struct VisualProperties {
        RGBA color = RGBA::White();
        std::function<RGBA(T)> color_func = nullptr;
        float line_width = 1.0f;
        float point_radius = 5.0f;
        float blur_radius = 0.0f;
        bool filled = false;
    };

    // 虚表定义
    template <typename T>
    struct ObjectVTable {
        void (*solver)(ObjectGraph<T>&, Object<T>&) = nullptr;
        void (*plotter)(ObjectGraph<T>&, Object<T>&) = nullptr;
    };

    // 核心模型对象 (彻底无 ID 化，双向指针连线，绝对安全)
    template <typename T>
    struct Object {
        NodeType type = NodeType::UNKNOWN;
        mutable uint64_t mask = 0;
        std::string name;
        uint64_t id{};
        // 样式配置
        mutable VisualProperties<T> style;


        utils::BlockDeque<Object*, 4> parents;
        utils::BlockDeque<Object*, 16> children;




        // 核心代数数据 Union
        union {
            struct { T x, y; } point_2d;
            struct { T x0, y0, x1, y1; } line_2d;
            struct { T a, b, c, d; } plane_3d;
            struct {T v0,v1,v2,v3,v4,v5,v6,v7;} test;
        } data;

        const ObjectVTable<T>* vptr = nullptr;
        ObjectGraph<T>* graph = nullptr; // 反向指针

        void set_mask(NodeMask m) const noexcept { mask |= static_cast<uint64_t>(m); }
        void clear_mask(NodeMask m) const noexcept { mask &= ~static_cast<uint64_t>(m); }
        [[nodiscard]] bool has_mask(NodeMask m) const noexcept { return (mask & static_cast<uint64_t>(m)) != 0; }

        // ========================================================================
        // 视觉效果链式配置方法
        // ========================================================================
        const Object* Color(RGBA col) const {
            style.color = col;
            style.color_func = nullptr;
            NotifyDirty();
            return this;
        }

        const Object* Color(std::function<RGBA(T)> func) const {
            style.color_func = std::move(func);
            NotifyDirty();
            return this;
        }

        const Object* Width(float w) const {
            style.line_width = w;
            NotifyDirty();
            return this;
        }

        const Object* PointRadius(float r) const {
            style.point_radius = r;
            NotifyDirty();
            return this;
        }

        const Object* Blur(float b) const {
            style.blur_radius = b;
            NotifyDirty();
            return this;
        }

        const Object* Filled(bool f) const {
            style.filled = f;
            NotifyDirty();
            return this;
        }

    private:
        // 【核心修正】：仅将节点本身标记为脏。
        // 不做任何全局图脏化，不做任何写时广播，彻底贯彻“拉模型”的极简主义
        void NotifyDirty() const noexcept {
            set_mask(NodeMask::DIRTY);
        }
    };
}