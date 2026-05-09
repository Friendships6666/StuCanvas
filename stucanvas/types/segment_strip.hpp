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

    // ==========================================
    // GPU 专用折线带（仅顶点数据，无 closed 标志）
    // ==========================================

    // 2D GPU 折线带（顶点为 Point2D_GPU，alignas(8) 确保对齐）
    struct SegmentStrip2D_GPU {
        std::vector<Point2D_GPU> vertices;

        // 序列化格式：点数 + 点数组
        size_t SerializedSize() const {
            return sizeof(uint32_t) + vertices.size() * sizeof(Point2D_GPU);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(vertices.size());
            std::memcpy(dst, &num, sizeof(num)); dst += sizeof(num);
            if (num > 0) {
                std::memcpy(dst, vertices.data(), num * sizeof(Point2D_GPU));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t num = 0;
            std::memcpy(&num, src, sizeof(num)); src += sizeof(num);
            if (size < sizeof(uint32_t) + num * sizeof(Point2D_GPU)) return;
            vertices.resize(num);
            if (num > 0) {
                std::memcpy(vertices.data(), src, num * sizeof(Point2D_GPU));
            }
        }
    };

    // 3D GPU 折线带（顶点为 Point3D_GPU，alignas(16)）
    struct SegmentStrip3D_GPU {
        std::vector<Point3D_GPU> vertices;

        size_t SerializedSize() const {
            return sizeof(uint32_t) + vertices.size() * sizeof(Point3D_GPU);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t num = static_cast<uint32_t>(vertices.size());
            std::memcpy(dst, &num, sizeof(num)); dst += sizeof(num);
            if (num > 0) {
                std::memcpy(dst, vertices.data(), num * sizeof(Point3D_GPU));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t num = 0;
            std::memcpy(&num, src, sizeof(num)); src += sizeof(num);
            if (size < sizeof(uint32_t) + num * sizeof(Point3D_GPU)) return;
            vertices.resize(num);
            if (num > 0) {
                std::memcpy(vertices.data(), src, num * sizeof(Point3D_GPU));
            }
        }
    };

} // namespace StuCanvas