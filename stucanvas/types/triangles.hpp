// stucanvas/types/triangles.hpp
#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "point.hpp"   // 需包含 Point2D, Point3D, Point2D_GPU, Point3D_GPU
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

    // ==========================================
    // 3D 三角形集合（无颜色）
    // ==========================================
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


    struct Triangles3D_GPU
    {
        std::vector<Point3D_GPU> points;
        std::vector<uint32_t>    indices;
        std::vector<Eigen::Vector3f> face_normals;   // 每个三角形一个法向量

        void Clear() {
            points.clear();
            indices.clear();
            face_normals.clear();
        }

        size_t SerializedSize() const {
            return sizeof(uint32_t) + points.size() * sizeof(Point3D_GPU)
                 + sizeof(uint32_t) + indices.size() * sizeof(uint32_t)
                 + sizeof(uint32_t) + face_normals.size() * sizeof(Eigen::Vector3f);
        }

        void Serialize(void* buffer) const {
            auto* dst = static_cast<uint8_t*>(buffer);

            // 写入顶点数据
            uint32_t pcount = static_cast<uint32_t>(points.size());
            std::memcpy(dst, &pcount, sizeof(pcount)); dst += sizeof(pcount);
            if (pcount > 0) {
                std::memcpy(dst, points.data(), pcount * sizeof(Point3D_GPU));
                dst += pcount * sizeof(Point3D_GPU);
            }

            // 写入索引数据
            uint32_t icount = static_cast<uint32_t>(indices.size());
            std::memcpy(dst, &icount, sizeof(icount)); dst += sizeof(icount);
            if (icount > 0) {
                std::memcpy(dst, indices.data(), icount * sizeof(uint32_t));
                dst += icount * sizeof(uint32_t);
            }

            // 写入面法向量数据
            uint32_t ncount = static_cast<uint32_t>(face_normals.size());
            std::memcpy(dst, &ncount, sizeof(ncount)); dst += sizeof(ncount);
            if (ncount > 0) {
                // Eigen::Vector3f 内存布局为连续 3 个 float，可直接拷贝
                std::memcpy(dst, face_normals.data(), ncount * sizeof(Eigen::Vector3f));
            }
        }

        void Deserialize(const void* buffer, size_t size) {
            const auto* src = static_cast<const uint8_t*>(buffer);
            size_t offset = 0;

            // 读取顶点数量和数据
            if (offset + sizeof(uint32_t) > size) return;
            uint32_t pcount = 0;
            std::memcpy(&pcount, src + offset, sizeof(pcount)); offset += sizeof(pcount);
            size_t points_bytes = pcount * sizeof(Point3D_GPU);
            if (offset + points_bytes > size) return;
            points.resize(pcount);
            if (pcount > 0) {
                std::memcpy(points.data(), src + offset, points_bytes);
                offset += points_bytes;
            }

            // 读取索引数量和数据
            if (offset + sizeof(uint32_t) > size) return;
            uint32_t icount = 0;
            std::memcpy(&icount, src + offset, sizeof(icount)); offset += sizeof(icount);
            size_t indices_bytes = icount * sizeof(uint32_t);
            if (offset + indices_bytes > size) return;
            indices.resize(icount);
            if (icount > 0) {
                std::memcpy(indices.data(), src + offset, indices_bytes);
                offset += indices_bytes;
            }

            // 读取法向量数量和数据
            if (offset + sizeof(uint32_t) > size) return;
            uint32_t ncount = 0;
            std::memcpy(&ncount, src + offset, sizeof(ncount)); offset += sizeof(ncount);
            size_t normals_bytes = ncount * sizeof(Eigen::Vector3f);
            if (offset + normals_bytes > size) return;
            face_normals.resize(ncount);
            if (ncount > 0) {
                std::memcpy(face_normals.data(), src + offset, normals_bytes);
                // offset 不再需要增加，但为了对称可加
            }
        }

    };

} // namespace StuCanvas