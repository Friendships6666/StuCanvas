/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/
#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include "../types/mesh.hpp"
#include "../types/point.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

/**
 * @brief 独立体素表面重构器 (Voxel Surface Reconstructor)
 *
 * 功能：将任意封闭的表面点云（壳）转化为 3D 块状网格 (Mesh3D)。
 * 解耦设计：不依赖 Graph 或 CSG 系统，仅需点云和体素尺寸。
 */
template <typename T>
class VoxelSurfaceReconstructor {
private:
    // 内部区间结构，用于高效存储实心跨度
    struct Span {
        int64_t start, end;
        bool operator<(const Span& o) const { return start < o.start; }
    };

    // 空间哈希：(iy, iz) -> 该行所有的实心区间
    using InternalMap = StuCanvas::utils::FlatMap<uint64_t, std::vector<Span>>;

    /**
     * @brief 内部函数：将点云通过奇偶规则转化为区间图 (实心化)
     */
    static InternalMap SolidifyToMap(const std::vector<Point3D<T>>& points, T size) {
        // 1. 分组扫描线
        StuCanvas::utils::FlatMap<uint64_t, std::vector<int64_t>> lines(points.size() / 4);
        for (const auto& p : points) {
            int64_t ix = static_cast<int64_t>(std::floor(p.x / size));
            int64_t iy = static_cast<int64_t>(std::floor(p.y / size));
            int64_t iz = static_cast<int64_t>(std::floor(p.z / size));
            uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(iy)) << 32) | static_cast<uint32_t>(iz);
            lines[key].push_back(ix);
        }

        InternalMap result_map(lines.size());

        // 2. 奇偶填充并压缩为区间
        for (auto it = lines.begin(); it != lines.end(); ++it) {
            auto& x_list = it->second;
            std::sort(x_list.begin(), x_list.end());
            x_list.erase(std::unique(x_list.begin(), x_list.end()), x_list.end());

            if (x_list.empty()) continue;

            // 聚类厚边界块
            std::vector<std::pair<int64_t, int64_t>> clusters;
            int64_t s = x_list[0], e = x_list[0];
            for (size_t i = 1; i < x_list.size(); ++i) {
                if (x_list[i] == e + 1) e = x_list[i];
                else { clusters.push_back({s, e}); s = x_list[i]; e = x_list[i]; }
            }
            clusters.push_back({s, e});

            // 生成并压缩区间
            std::vector<Span> spans;
            for (size_t i = 0; i < clusters.size(); ++i) {
                // 边界块
                spans.push_back({clusters[i].first, clusters[i].second});
                // 奇偶填充内部
                if (i % 2 == 0 && (i + 1) < clusters.size()) {
                    spans.push_back({clusters[i].second + 1, clusters[i+1].first - 1});
                }
            }
            // 最终合并所有连续区间
            std::sort(spans.begin(), spans.end());
            std::vector<Span> merged;
            if(!spans.empty()){
                merged.push_back(spans[0]);
                for(size_t i=1; i<spans.size(); ++i){
                    if(spans[i].start <= merged.back().end + 1)
                        merged.back().end = std::max(merged.back().end, spans[i].end);
                    else merged.push_back(spans[i]);
                }
            }
            result_map.insert(it->first, std::move(merged));
        }
        return result_map;
    }

    static bool CheckSolid(const InternalMap& map, int64_t x, int64_t y, int64_t z) {
        uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(z);
        auto it = map.find(key);
        if (it == map.end()) return false;
        const auto& spans = it->second;
        auto comp = [](const Span& s, int64_t val) { return s.end < val; };
        auto bound = std::lower_bound(spans.begin(), spans.end(), x, comp);
        return (bound != spans.end() && x >= bound->start && x <= bound->end);
    }

    static void AddQuad(Mesh3D<T>& mesh, int64_t x, int64_t y, int64_t z, int side, T size) {
        T x0 = static_cast<T>(x) * size, x1 = x0 + size;
        T y0 = static_cast<T>(y) * size, y1 = y0 + size;
        T z0 = static_cast<T>(z) * size, z1 = z0 + size;
        uint32_t v = static_cast<uint32_t>(mesh.vertices.size());
        switch (side) {
            case 0: mesh.vertices.push_back({{x0, y0, z1}}); mesh.vertices.push_back({{x0, y0, z0}}); mesh.vertices.push_back({{x0, y1, z0}}); mesh.vertices.push_back({{x0, y1, z1}}); break; // -X
            case 1: mesh.vertices.push_back({{x1, y0, z0}}); mesh.vertices.push_back({{x1, y0, z1}}); mesh.vertices.push_back({{x1, y1, z1}}); mesh.vertices.push_back({{x1, y1, z0}}); break; // +X
            case 2: mesh.vertices.push_back({{x0, y0, z0}}); mesh.vertices.push_back({{x1, y0, z0}}); mesh.vertices.push_back({{x1, y0, z1}}); mesh.vertices.push_back({{x0, y0, z1}}); break; // -Y
            case 3: mesh.vertices.push_back({{x0, y1, z1}}); mesh.vertices.push_back({{x1, y1, z1}}); mesh.vertices.push_back({{x1, y1, z0}}); mesh.vertices.push_back({{x0, y1, z0}}); break; // +Y
            case 4: mesh.vertices.push_back({{x1, y0, z0}}); mesh.vertices.push_back({{x0, y0, z0}}); mesh.vertices.push_back({{x0, y1, z0}}); mesh.vertices.push_back({{x1, y1, z0}}); break; // -Z
            case 5: mesh.vertices.push_back({{x0, y0, z1}}); mesh.vertices.push_back({{x1, y0, z1}}); mesh.vertices.push_back({{x1, y1, z1}}); mesh.vertices.push_back({{x0, y1, z1}}); break; // +Z
        }
        mesh.AddTriangle(v, v+1, v+2); mesh.AddTriangle(v, v+2, v+3);
    }

public:
    /**
     * @brief 主接口：从点云生成 Mesh
     * @param points 封闭的点云外壳
     * @param voxel_size 体素大小（建议设为点云平均间距的 1.5 - 2.0 倍）
     */
    static Mesh3D<T> Reconstruct(const std::vector<Point3D<T>>& points, T voxel_size) {
        Mesh3D<T> mesh;
        if (points.empty()) return mesh;

        // 1. 实心化：建立空间区间索引
        InternalMap map = SolidifyToMap(points, voxel_size);

        // 2. 遍历实心区间，执行面剔除提取
        for (auto it = map.begin(); it != map.end(); ++it) {
            int32_t iy = static_cast<int32_t>(it->first >> 32);
            int32_t iz = static_cast<int32_t>(it->first & 0xFFFFFFFF);
            for (const auto& span : it->second) {
                for (int64_t ix = span.start; ix <= span.end; ++ix) {
                    // 检查 6 个邻居，若邻居为空气，则该面可见
                    if (!CheckSolid(map, ix - 1, iy, iz)) AddQuad(mesh, ix, iy, iz, 0, voxel_size);
                    if (!CheckSolid(map, ix + 1, iy, iz)) AddQuad(mesh, ix, iy, iz, 1, voxel_size);
                    if (!CheckSolid(map, ix, iy - 1, iz)) AddQuad(mesh, ix, iy, iz, 2, voxel_size);
                    if (!CheckSolid(map, ix, iy + 1, iz)) AddQuad(mesh, ix, iy, iz, 3, voxel_size);
                    if (!CheckSolid(map, ix, iy, iz - 1)) AddQuad(mesh, ix, iy, iz, 4, voxel_size);
                    if (!CheckSolid(map, ix, iy, iz + 1)) AddQuad(mesh, ix, iy, iz, 5, voxel_size);
                }
            }
        }
        return mesh;
    }
};

} // namespace StuCanvas::Reconstruction