#pragma once

#include <vector>
#include <cmath>
#include "../types/mesh.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class MeshWelder {
private:
    // 空间哈希键：用于快速定位极近的顶点
    static inline uint64_t HashCoord(T x, T y, T z, T epsilon) {
        int64_t ix = static_cast<int64_t>(std::floor(x / epsilon));
        int64_t iy = static_cast<int64_t>(std::floor(y / epsilon));
        int64_t iz = static_cast<int64_t>(std::floor(z / epsilon));
        return (static_cast<uint64_t>(ix) * 73856093) ^ 
               (static_cast<uint64_t>(iy) * 19349663) ^ 
               (static_cast<uint64_t>(iz) * 83492791);
    }

    // 用于唯一标识一个三角形（不计顺序）
    struct FaceKey {
        uint32_t v[3];
        bool operator==(const FaceKey& o) const {
            return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2];
        }
    };

    static inline uint64_t HashFace(uint32_t v1, uint32_t v2, uint32_t v3) {
        uint32_t arr[3] = {v1, v2, v3};
        std::sort(arr, arr + 3);
        return (static_cast<uint64_t>(arr[0]) * 73856093) ^ 
               (static_cast<uint64_t>(arr[1]) * 19349663) ^ 
               (static_cast<uint64_t>(arr[2]) * 83492791);
    }

public:
    /**
     * @brief 缝合多个凸包 Mesh
     * @param hulls 凸分解产生的 Mesh 列表
     * @param weld_epsilon 焊接阈值（建议设为 voxel_size 的 0.1 倍）
     */
    static Mesh3D<T> Weld(const std::vector<Mesh3D<T>>& hulls, T weld_epsilon = 1e-6) {
        Mesh3D<T> result;
        if (hulls.empty()) return result;

        // 1. 顶点焊接映射表
        // Key: 空间哈希, Value: 新 Mesh 中的顶点索引
        StuCanvas::utils::FlatMap<uint64_t, uint32_t> vertex_map(hulls.size() * 30);
        std::vector<uint32_t> global_remap; // 存储每个原始顶点对应的新索引

        // 遍历所有 Mesh 的顶点进行焊接
        for (const auto& h : hulls) {
            for (const auto& v : h.vertices) {
                uint64_t key = HashCoord(v.position.x, v.position.y, v.position.z, weld_epsilon);
                auto it = vertex_map.find(key);
                
                if (it == vertex_map.end()) {
                    // 新顶点
                    uint32_t new_idx = static_cast<uint32_t>(result.vertices.size());
                    result.vertices.push_back(v);
                    vertex_map.insert(key, new_idx);
                    global_remap.push_back(new_idx);
                } else {
                    // 已存在的邻近顶点，复用索引
                    global_remap.push_back(it->second);
                }
            }
        }

        // 2. 内部面剔除 (Double-Face Culling)
        // 统计每个面出现的次数。如果一个面在焊接后出现了 2 次，说明它是两个凸块的接触面。
        StuCanvas::utils::FlatMap<uint64_t, int> face_count_map;
        
        uint32_t offset = 0;
        for (const auto& h : hulls) {
            for (size_t i = 0; i < h.indices.size(); i += 3) {
                uint32_t g_v1 = global_remap[offset + h.indices[i]];
                uint32_t g_v2 = global_remap[offset + h.indices[i+1]];
                uint32_t g_v3 = global_remap[offset + h.indices[i+2]];
                
                // 忽略退化三角形
                if (g_v1 == g_v2 || g_v2 == g_v3 || g_v3 == g_v1) continue;

                uint64_t f_key = HashFace(g_v1, g_v2, g_v3);
                face_count_map[f_key]++;
            }
            offset += h.vertices.size();
        }

        // 3. 重新填充索引表，仅保留只出现一次的面（外表面）
        offset = 0;
        for (const auto& h : hulls) {
            for (size_t i = 0; i < h.indices.size(); i += 3) {
                uint32_t g_v1 = global_remap[offset + h.indices[i]];
                uint32_t g_v2 = global_remap[offset + h.indices[i+1]];
                uint32_t g_v3 = global_remap[offset + h.indices[i+2]];
                
                if (g_v1 == g_v2 || g_v2 == g_v3 || g_v3 == g_v1) continue;

                uint64_t f_key = HashFace(g_v1, g_v2, g_v3);
                // 只有 Count == 1 的面才是模型的外边界壳
                if (face_count_map.find(f_key)->second == 1) {
                    result.indices.push_back(g_v1);
                    result.indices.push_back(g_v2);
                    result.indices.push_back(g_v3);
                }
            }
            offset += h.vertices.size();
        }

        return result;
    }
};

} // namespace StuCanvas::Reconstruction