#pragma once
#include <vector>
#include <algorithm>
#include "../types/point.hpp"
#include "../utils/flat_map.hpp"

namespace StuCanvas::Reconstruction {

template <typename T>
class VoxelIntervalEngine3D {
public:
    // 定义一个实心区间：[x_start, x_end] (单位是 grid index)
    struct Interval {
        int64_t start, end;
        bool operator<(const Interval& o) const { return start < o.start; }
    };

    // 每一行扫描线对应一组不相交的实心区间
    using LineIntervals = std::vector<Interval>;
    using IntervalMap = StuCanvas::utils::FlatMap<uint64_t, LineIntervals>;

private:
    static inline uint64_t HashLine(int64_t y, int64_t z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(z);
    }

    /**
     * @brief 核心：将表面点云壳压缩为极小的“扫描线区间表”
     * 内存占用：O(表面积)，远低于 O(体积)
     */
    static IntervalMap BuildIntervalMap(const std::vector<Point3D<T>>& shell, T voxel_size) {
        // 1. 分组扫描线
        StuCanvas::utils::FlatMap<uint64_t, std::vector<int64_t>> raw_lines(shell.size() / 10);
        for (const auto& p : shell) {
            int64_t ix = static_cast<int64_t>(std::floor(p.x / voxel_size));
            int64_t iy = static_cast<int64_t>(std::floor(p.y / voxel_size));
            int64_t iz = static_cast<int64_t>(std::floor(p.z / voxel_size));
            raw_lines[HashLine(iy, iz)].push_back(ix);
        }

        IntervalMap final_map(raw_lines.size());

        // 2. 对每一行执行奇偶填充并“压缩”成区间
        for (auto it = raw_lines.begin(); it != raw_lines.end(); ++it) {
            std::vector<int64_t>& x_list = it->second;
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

            // 生成区间
            LineIntervals line;
            for (size_t i = 0; i < clusters.size(); ++i) {
                // 边界块本身就是一个区间
                line.push_back({clusters[i].first, clusters[i].second});
                // 奇偶填充：在第 2n 和 2n+1 块之间建立实心区间
                if (i % 2 == 0 && (i + 1) < clusters.size()) {
                    line.push_back({clusters[i].second + 1, clusters[i+1].first - 1});
                }
            }
            // 再次合并相邻区间以实现最大压缩
            std::sort(line.begin(), line.end());
            LineIntervals compressed;
            if(!line.empty()) {
                compressed.push_back(line[0]);
                for(size_t i=1; i<line.size(); ++i) {
                    if(line[i].start <= compressed.back().end + 1)
                        compressed.back().end = std::max(compressed.back().end, line[i].end);
                    else compressed.push_back(line[i]);
                }
            }
            final_map.insert(it->first, std::move(compressed));
        }
        return final_map;
    }

    /**
     * @brief 极速分类判定：点是否在实心区间内
     * 复杂度：O(log(每行区间数))，通常近乎 O(1)
     */
    static bool IsInside(const Point3D<T>& p, const IntervalMap& map, T voxel_size) {
        int64_t ix = static_cast<int64_t>(std::floor(p.x / voxel_size));
        int64_t iy = static_cast<int64_t>(std::floor(p.y / voxel_size));
        int64_t iz = static_cast<int64_t>(std::floor(p.z / voxel_size));

        auto it = map.find(HashLine(iy, iz));
        if (it == map.end()) return false;

        const auto& intervals = it->second;
        // 使用二分查找快速定位区间
        auto comp = [](const Interval& intr, int64_t val) { return intr.end < val; };
        auto bound = std::lower_bound(intervals.begin(), intervals.end(), ix, comp);

        return (bound != intervals.end() && ix >= bound->start && ix <= bound->end);
    }

public:
    /**
     * @brief 高性能、保真表面布尔差集
     */
    static std::vector<Point3D<T>> FastSurfaceDifference(
        const std::vector<Point3D<T>>& A_shell,
        const std::vector<Point3D<T>>& B_shell,
        T voxel_size)
    {
        // 建立 B 的空间区间索引（极小，内存友好）
        IntervalMap mapB = BuildIntervalMap(B_shell, voxel_size);
        // 建立 A 的空间区间索引
        IntervalMap mapA = BuildIntervalMap(A_shell, voxel_size);

        std::vector<Point3D<T>> result;
        result.reserve(A_shell.size());

        // 1. 保留 A 中在 B 外面的原始点 (原封不动，无信息丢失)
        for (const auto& p : A_shell) {
            if (!IsInside(p, mapB, voxel_size)) result.push_back(p);
        }

        // 2. 提取 B 中在 A 内部的原始点 (作为孔洞内壁，原封不动)
        for (const auto& p : B_shell) {
            if (IsInside(p, mapA, voxel_size)) result.push_back(p);
        }

        return result;
    }
};

}