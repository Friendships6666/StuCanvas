#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include "../types/point.hpp"
#include "../types/mesh.hpp"
#include "../utils/flat_map.hpp"
#include "../utils/math_traits.hpp"
#include "../deprecated_reconstruction/boolean_engine.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class SimpleVoxelizer {
public:
    // ---------- 面剔除模式（保留，可用于快速预览）----------
    static Mesh3D<T> Reconstruct(const std::vector<Point3D<T>>& points, T voxel_size) {
        Mesh3D<T> mesh;
        if (points.empty() || voxel_size <= 0) return mesh;

        Point3D<T> bmin = points[0], bmax = points[0];
        for (const auto& p : points) {
            if (p.x < bmin.x) bmin.x = p.x;
            if (p.y < bmin.y) bmin.y = p.y;
            if (p.z < bmin.z) bmin.z = p.z;
            if (p.x > bmax.x) bmax.x = p.x;
            if (p.y > bmax.y) bmax.y = p.y;
            if (p.z > bmax.z) bmax.z = p.z;
        }
        bmin.x -= voxel_size; bmin.y -= voxel_size; bmin.z -= voxel_size;
        bmax.x += voxel_size; bmax.y += voxel_size; bmax.z += voxel_size;

        utils::FlatMap<uint64_t, bool> voxels(points.size());
        for (const auto& p : points) {
            int64_t ix = static_cast<int64_t>(std::floor((p.x - bmin.x) / voxel_size));
            int64_t iy = static_cast<int64_t>(std::floor((p.y - bmin.y) / voxel_size));
            int64_t iz = static_cast<int64_t>(std::floor((p.z - bmin.z) / voxel_size));
            voxels[pack(ix, iy, iz)] = true;
        }

        for (auto it = voxels.begin(); it != voxels.end(); ++it) {
            int64_t x, y, z;
            unpack(it->first, x, y, z);
            if (!voxels.contains(pack(x - 1, y, z))) AddFace(mesh, x, y, z, 0, voxel_size, bmin);
            if (!voxels.contains(pack(x + 1, y, z))) AddFace(mesh, x, y, z, 1, voxel_size, bmin);
            if (!voxels.contains(pack(x, y - 1, z))) AddFace(mesh, x, y, z, 2, voxel_size, bmin);
            if (!voxels.contains(pack(x, y + 1, z))) AddFace(mesh, x, y, z, 3, voxel_size, bmin);
            if (!voxels.contains(pack(x, y, z - 1))) AddFace(mesh, x, y, z, 4, voxel_size, bmin);
            if (!voxels.contains(pack(x, y, z + 1))) AddFace(mesh, x, y, z, 5, voxel_size, bmin);
        }
        return mesh;
    }

    // ---------- 生成完整的独立立方体列表 ----------
    static std::vector<Mesh3D<T>> ReconstructAsBlocks(const std::vector<Point3D<T>>& points, T voxel_size) {
        std::vector<Mesh3D<T>> blocks;
        if (points.empty() || voxel_size <= 0) return blocks;

        Point3D<T> bmin = points[0], bmax = points[0];
        for (const auto& p : points) {
            if (p.x < bmin.x) bmin.x = p.x; if (p.y < bmin.y) bmin.y = p.y; if (p.z < bmin.z) bmin.z = p.z;
            if (p.x > bmax.x) bmax.x = p.x; if (p.y > bmax.y) bmax.y = p.y; if (p.z > bmax.z) bmax.z = p.z;
        }
        bmin.x -= voxel_size; bmin.y -= voxel_size; bmin.z -= voxel_size;
        bmax.x += voxel_size; bmax.y += voxel_size; bmax.z += voxel_size;

        utils::FlatMap<uint64_t, bool> voxels(points.size());
        for (const auto& p : points) {
            int64_t ix = static_cast<int64_t>(std::floor((p.x - bmin.x) / voxel_size));
            int64_t iy = static_cast<int64_t>(std::floor((p.y - bmin.y) / voxel_size));
            int64_t iz = static_cast<int64_t>(std::floor((p.z - bmin.z) / voxel_size));
            voxels[pack(ix, iy, iz)] = true;
        }

        blocks.reserve(voxels.size());
        for (auto it = voxels.begin(); it != voxels.end(); ++it) {
            int64_t x, y, z;
            unpack(it->first, x, y, z);
            Mesh3D<T> cube;
            BuildCube(cube, x, y, z, voxel_size, bmin);
            blocks.push_back(std::move(cube));
        }
        return blocks;
    }

    // ---------- 批量布尔并集：直接生成光滑外壳 ----------
    static Mesh3D<T> ReconstructSmooth(const std::vector<Point3D<T>>& points, T voxel_size) {
        auto blocks = ReconstructAsBlocks(points, voxel_size);
        if (blocks.empty()) return Mesh3D<T>();
        return Reconstruction::BooleanEngine<T>::UnionMultiple(blocks);
    }

private:
    // 坐标打包/解包
    static uint64_t pack(int64_t x, int64_t y, int64_t z) {
        uint64_t ux = static_cast<uint64_t>(x + (1 << 19));
        uint64_t uy = static_cast<uint64_t>(y + (1 << 19));
        uint64_t uz = static_cast<uint64_t>(z + (1 << 19));
        return (ux << 42) | (uy << 21) | uz;
    }

    static void unpack(uint64_t key, int64_t& x, int64_t& y, int64_t& z) {
        uint64_t uz = key & 0x1FFFFF;
        uint64_t uy = (key >> 21) & 0x1FFFFF;
        uint64_t ux = (key >> 42) & 0x1FFFFF;
        x = static_cast<int64_t>(ux) - (1 << 19);
        y = static_cast<int64_t>(uy) - (1 << 19);
        z = static_cast<int64_t>(uz) - (1 << 19);
    }

    // 添加一个方块的面（用于面剔除模式）
    static void AddFace(Mesh3D<T>& mesh, int64_t x, int64_t y, int64_t z, int side,
                        T size, const Point3D<T>& origin) {
        T x0 = origin.x + static_cast<T>(x) * size;
        T y0 = origin.y + static_cast<T>(y) * size;
        T z0 = origin.z + static_cast<T>(z) * size;
        T x1 = x0 + size, y1 = y0 + size, z1 = z0 + size;
        uint32_t v = static_cast<uint32_t>(mesh.vertices.size());

        switch (side) {
            case 0: // -X
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z1});
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z0});
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z1});
                break;
            case 1: // +X
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z0});
                break;
            case 2: // -Y
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z1});
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z1});
                break;
            case 3: // +Y
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z0});
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z0});
                break;
            case 4: // -Z
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z0});
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z0});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z0});
                break;
            case 5: // +Z
                mesh.vertices.emplace_back(Point3D<T>{x0, y0, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y0, z1});
                mesh.vertices.emplace_back(Point3D<T>{x1, y1, z1});
                mesh.vertices.emplace_back(Point3D<T>{x0, y1, z1});
                break;
        }
        mesh.AddTriangle(v, v + 1, v + 2);
        mesh.AddTriangle(v, v + 2, v + 3);
    }

    // 构建完整的正方体网格
    static void BuildCube(Mesh3D<T>& mesh, int64_t x, int64_t y, int64_t z, T size, const Point3D<T>& origin) {
        T x0 = origin.x + static_cast<T>(x) * size;
        T y0 = origin.y + static_cast<T>(y) * size;
        T z0 = origin.z + static_cast<T>(z) * size;
        T x1 = x0 + size, y1 = y0 + size, z1 = z0 + size;

        // 8 个顶点
        mesh.vertices = {
            {{x0, y0, z0}}, {{x1, y0, z0}}, {{x1, y1, z0}}, {{x0, y1, z0}},  // 底面
            {{x0, y0, z1}}, {{x1, y0, z1}}, {{x1, y1, z1}}, {{x0, y1, z1}}   // 顶面
        };
        // 12 个三角形（逆时针，法线朝外）
        mesh.indices = {
            // 底面
            0, 2, 1, 0, 3, 2,
            // 顶面
            4, 5, 6, 4, 6, 7,
            // 前面
            0, 1, 5, 0, 5, 4,
            // 后面
            2, 3, 7, 2, 7, 6,
            // 左面
            3, 0, 4, 3, 4, 7,
            // 右面
            1, 2, 6, 1, 6, 5
        };
    }
};

} // namespace StuCanvas::Reconstruction