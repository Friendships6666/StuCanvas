// stucanvas/types/segment_strip.hpp
#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "point.hpp"   // 需包含 Point2D, Point3D, Point2D_GPU, Point3D_GPU

namespace StuCanvas {

    // ==========================================
    // 2D 折线带（模板版本）
    // ==========================================
    template <typename T>
    struct SegmentStrip2D {
        using Scalar = T;
        std::vector<Point2D<T>> vertices;
        bool closed = false;

        // ---------- 序列化支持（适配 StuCache） ----------
        size_t SerializedSize() const {
            // 格式：点数(uint32_t) + closed(uint8_t) + 顶点数组
            return sizeof(uint32_t) + sizeof(uint8_t) + vertices.size() * sizeof(Point2D<T>);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(vertices.size());
            std::memcpy(dst, &num, sizeof(num)); dst += sizeof(num);
            uint8_t closed_flag = closed ? 1 : 0;
            *dst++ = closed_flag;
            if (num > 0) {
                std::memcpy(dst, vertices.data(), num * sizeof(Point2D<T>));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t num = 0;
            std::memcpy(&num, src, sizeof(num)); src += sizeof(num);
            if (size < sizeof(uint32_t) + sizeof(uint8_t)) return;
            uint8_t closed_flag = *src++;
            closed = (closed_flag != 0);
            size_t required = sizeof(uint32_t) + sizeof(uint8_t) + num * sizeof(Point2D<T>);
            if (size < required) return;
            vertices.resize(num);
            if (num > 0) {
                std::memcpy(vertices.data(), src, num * sizeof(Point2D<T>));
            }
        }
    };

    // ==========================================
    // 3D 折线带（模板版本）
    // ==========================================
    template <typename T>
    struct SegmentStrip3D {
        using Scalar = T;
        std::vector<Point3D<T>> vertices;
        bool closed = false;

        size_t SerializedSize() const {
            return sizeof(uint32_t) + sizeof(uint8_t) + vertices.size() * sizeof(Point3D<T>);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(vertices.size());
            std::memcpy(dst, &num, sizeof(num)); dst += sizeof(num);
            uint8_t closed_flag = closed ? 1 : 0;
            *dst++ = closed_flag;
            if (num > 0) {
                std::memcpy(dst, vertices.data(), num * sizeof(Point3D<T>));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t num = 0;
            std::memcpy(&num, src, sizeof(num)); src += sizeof(num);
            if (size < sizeof(uint32_t) + sizeof(uint8_t)) return;
            uint8_t closed_flag = *src++;
            closed = (closed_flag != 0);
            size_t required = sizeof(uint32_t) + sizeof(uint8_t) + num * sizeof(Point3D<T>);
            if (size < required) return;
            vertices.resize(num);
            if (num > 0) {
                std::memcpy(vertices.data(), src, num * sizeof(Point3D<T>));
            }
        }
    };

    struct alignas(16) SegmentGPU {
        float startX, startY, startZ;
        float _pad0;                  // 必须填充

        float endX, endY, endZ;
        float _pad1;                  // 必须填充

        float startR, startG, startB, startA; // 起点 RGBA
        float endR,   endG,   endB,   endA;   // 终点 RGBA
    };


} // namespace StuCanvas