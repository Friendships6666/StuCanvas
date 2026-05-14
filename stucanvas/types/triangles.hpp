// stucanvas/types/triangles.hpp
#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "point.hpp"   // 需包含 Point2D, Point3D, Point2D_GPU, PointGPU
#include <Eigen/Dense>           // 需要 Eigen 库
namespace StuCanvas
{
    // ==========================================
    // 2D 三角形集合（无颜色）
    // ==========================================
    template <typename T>
    struct Triangles2D {
        std::vector<Point2D<T>> points;
        std::vector<uint32_t>   indices;

        void Clear() {
            points.clear();
            indices.clear();
        }

        // ---------- 序列化支持 ----------
        size_t SerializedSize() const {
            return sizeof(uint32_t) + points.size() * sizeof(Point2D<T>)
                 + sizeof(uint32_t) + indices.size() * sizeof(uint32_t);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            // 写入 points
            uint32_t pcount = static_cast<uint32_t>(points.size());
            std::memcpy(dst, &pcount, sizeof(pcount)); dst += sizeof(pcount);
            if (pcount > 0) {
                std::memcpy(dst, points.data(), pcount * sizeof(Point2D<T>));
                dst += pcount * sizeof(Point2D<T>);
            }
            // 写入 indices
            uint32_t icount = static_cast<uint32_t>(indices.size());
            std::memcpy(dst, &icount, sizeof(icount)); dst += sizeof(icount);
            if (icount > 0) {
                std::memcpy(dst, indices.data(), icount * sizeof(uint32_t));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            // 读取 points 数量
            uint32_t pcount = 0;
            std::memcpy(&pcount, src, sizeof(pcount)); src += sizeof(pcount);
            size_t points_bytes = pcount * sizeof(Point2D<T>);
            if (size < sizeof(uint32_t) + points_bytes + sizeof(uint32_t)) return;
            // 读取 points 数据
            points.resize(pcount);
            if (pcount > 0) {
                std::memcpy(points.data(), src, points_bytes);
                src += points_bytes;
            }
            // 读取 indices 数量
            uint32_t icount = 0;
            std::memcpy(&icount, src, sizeof(icount)); src += sizeof(icount);
            size_t required = sizeof(uint32_t) + points_bytes + sizeof(uint32_t) + icount * sizeof(uint32_t);
            if (size < required) return;
            indices.resize(icount);
            if (icount > 0) {
                std::memcpy(indices.data(), src, icount * sizeof(uint32_t));
            }
        }
    };

    template <typename T>
    struct Triangles3D {
        std::vector<Point3D<T>> points;
        std::vector<uint32_t>   indices;

        void Clear() {
            points.clear();
            indices.clear();
        }

        size_t SerializedSize() const {
            return sizeof(uint32_t) + points.size() * sizeof(Point3D<T>)
                 + sizeof(uint32_t) + indices.size() * sizeof(uint32_t);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);
            uint32_t pcount = static_cast<uint32_t>(points.size());
            std::memcpy(dst, &pcount, sizeof(pcount)); dst += sizeof(pcount);
            if (pcount > 0) {
                std::memcpy(dst, points.data(), pcount * sizeof(Point3D<T>));
                dst += pcount * sizeof(Point3D<T>);
            }
            uint32_t icount = static_cast<uint32_t>(indices.size());
            std::memcpy(dst, &icount, sizeof(icount)); dst += sizeof(icount);
            if (icount > 0) {
                std::memcpy(dst, indices.data(), icount * sizeof(uint32_t));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            if (size < sizeof(uint32_t)) return;
            uint32_t pcount = 0;
            std::memcpy(&pcount, src, sizeof(pcount)); src += sizeof(pcount);
            size_t points_bytes = pcount * sizeof(Point3D<T>);
            if (size < sizeof(uint32_t) + points_bytes + sizeof(uint32_t)) return;
            points.resize(pcount);
            if (pcount > 0) {
                std::memcpy(points.data(), src, points_bytes);
                src += points_bytes;
            }
            uint32_t icount = 0;
            std::memcpy(&icount, src, sizeof(icount)); src += sizeof(icount);
            size_t required = sizeof(uint32_t) + points_bytes + sizeof(uint32_t) + icount * sizeof(uint32_t);
            if (size < required) return;
            indices.resize(icount);
            if (icount > 0) {
                std::memcpy(indices.data(), src, icount * sizeof(uint32_t));
            }
        }
    };



    };
