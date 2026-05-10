// stucanvas/types/path.hpp
#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "point.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Path2D
    {
        using Point = Point2D<T>;

        // 控制点序列：每 4 个连续点构成一段三阶贝塞尔（起点、控制点1、控制点2、终点）
        std::vector<Point> control_points;

        // 是否闭合（闭合时最后一段终点应与第一段起点相连，渲染时会自动闭合）
        bool closed = false;

        // ---------- 序列化支持（适配 StuCache） ----------
        // 计算序列化所需的字节数
        size_t SerializedSize() const
        {
            // 格式：点数(uint32_t) + closed(uint8_t) + points 数组
            return sizeof(uint32_t) + sizeof(uint8_t) + control_points.size() * sizeof(Point);
        }

        // 序列化到预先分配的 buffer（buffer 大小至少为 SerializedSize()）
        void Serialize(void* buffer) const
        {
            auto* dst = static_cast<uint8_t*>(buffer);
            // 写入点数
            uint32_t num = static_cast<uint32_t>(control_points.size());
            std::memcpy(dst, &num, sizeof(num));
            dst += sizeof(num);
            // 写入闭合标志
            uint8_t closed_flag = closed ? 1 : 0;
            *dst++ = closed_flag;
            // 写入点数组
            if (num > 0)
            {
                std::memcpy(dst, control_points.data(), num * sizeof(Point));
            }
        }

        // 从二进制 buffer 反序列化（buffer 格式需与 Serialize 一致）
        void Deserialize(const void* buffer, size_t size)
        {
            const auto* src = static_cast<const uint8_t*>(buffer);
            // 读取点数
            uint32_t num = 0;
            if (size < sizeof(num)) return; // 数据不完整
            std::memcpy(&num, src, sizeof(num));
            src += sizeof(num);
            // 读取闭合标志
            if (size < sizeof(num) + sizeof(uint8_t)) return;
            uint8_t closed_flag = *src++;
            closed = (closed_flag != 0);
            // 检查缓冲区大小
            size_t required = sizeof(num) + sizeof(uint8_t) + num * sizeof(Point);
            if (size < required) return;
            // 读取点数组
            control_points.resize(num);
            if (num > 0)
            {
                std::memcpy(control_points.data(), src, num * sizeof(Point));
            }
        }
    };

    template <typename T>
    struct Path3D
    {
        using Point = Point3D<T>;

        std::vector<Point> control_points;

        // 是否闭合
        bool closed = false;

        // ---------- 序列化支持 ----------
        size_t SerializedSize() const
        {
            return sizeof(uint32_t) + sizeof(uint8_t) + control_points.size() * sizeof(Point);
        }

        void Serialize(void* buffer) const
        {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(control_points.size());
            std::memcpy(dst, &num, sizeof(num));
            dst += sizeof(num);
            uint8_t closed_flag = closed ? 1 : 0;
            *dst++ = closed_flag;
            if (num > 0)
            {
                std::memcpy(dst, control_points.data(), num * sizeof(Point));
            }
        }

        void Deserialize(const void* buffer, size_t size)
        {
            const auto* src = static_cast<const uint8_t*>(buffer);
            uint32_t num = 0;
            if (size < sizeof(num)) return;
            std::memcpy(&num, src, sizeof(num));
            src += sizeof(num);
            if (size < sizeof(num) + sizeof(uint8_t)) return;
            uint8_t closed_flag = *src++;
            closed = (closed_flag != 0);
            size_t required = sizeof(num) + sizeof(uint8_t) + num * sizeof(Point);
            if (size < required) return;
            control_points.resize(num);
            if (num > 0)
            {
                std::memcpy(control_points.data(), src, num * sizeof(Point));
            }
        }
    };



    struct Path3D_GPU
    {
        std::vector<Point3D_GPU> control_points;

        // --- 序列化支持 ---
        size_t SerializedSize() const
        {
            return sizeof(uint32_t) + control_points.size() * sizeof(Point3D_GPU);
        }

        void Serialize(void* buffer) const
        {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(control_points.size());
            std::memcpy(dst, &num, sizeof(num));
            dst += sizeof(num);
            if (num > 0)
            {
                std::memcpy(dst, control_points.data(), num * sizeof(Point3D_GPU));
            }
        }

        void Deserialize(const void* buffer, size_t size)
        {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t num = 0;
            std::memcpy(&num, src, sizeof(num));
            src += sizeof(num);
            if (size < sizeof(uint32_t) + num * sizeof(Point3D_GPU)) return;
            control_points.resize(num);
            if (num > 0)
            {
                std::memcpy(control_points.data(), src, num * sizeof(Point3D_GPU));
            }
        }
    };
} // namespace StuCanvas
