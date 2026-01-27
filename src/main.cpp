#include <iostream>
#include <vector>
#include <span>
#include <optional>
#include <chrono>
#include <cstdint>
#include <cmath>

struct Point {
    int16_t x, y;
};

// 打包/解包保持高效位操作
inline uint32_t pack(Point p) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(p.x)) << 16) |
            static_cast<uint16_t>(p.y);
}

inline Point unpack(uint32_t packed) {
    return { static_cast<int16_t>(packed >> 16),
             static_cast<int16_t>(packed & 0xFFFF) };
}

// 极致优化的线性探测哈希表
struct FlatIntersectionMap {
    static constexpr uint32_t EMPTY = 0xFFFFFFFF;
    struct Entry {
        uint32_t key = EMPTY;
        uint16_t count = 0;
    };

    std::vector<Entry> table;
    uint32_t mask;

    FlatIntersectionMap(size_t expected_elements) {
        size_t capacity = 1;
        // 保持低负载因子 (0.5) 以减少线性探测冲突
        while (capacity < expected_elements * 2) capacity <<= 1;
        table.resize(capacity);
        mask = capacity - 1;
    }

    // 核心插入逻辑：每个对象只贡献一票
    inline void vote(uint32_t key, uint16_t current_obj_idx) {
        uint32_t h = key & mask; // Identity Hash
        while (table[h].key != EMPTY) {
            if (table[h].key == key) {
                if (table[h].count == current_obj_idx) {
                    table[h].count++;
                }
                return;
            }
            h = (h + 1) & mask;
        }
        if (current_obj_idx == 0) {
            table[h].key = key;
            table[h].count = 1;
        }
    }
};

std::optional<Point> find_nearest_optimized(
    const std::vector<Point>& all_points,
    const std::vector<size_t>& offsets,
    Point anchor)
{
    if (offsets.empty()) return std::nullopt;

    const size_t num_objects = offsets.size();
    size_t first_seg_size = (num_objects > 1 ? offsets[1] : all_points.size()) - offsets[0];

    FlatIntersectionMap hit_map(first_seg_size);

    // 1. 投票阶段 O(N)
    for (size_t i = 0; i < num_objects; ++i) {
        size_t start = offsets[i];
        size_t end = (i + 1 < num_objects) ? offsets[i + 1] : all_points.size();
        auto segment = std::span(all_points.data() + start, end - start);

        for (const auto& p : segment) {
            hit_map.vote(pack(p), static_cast<uint16_t>(i));
        }
    }

    // 2. 距离筛选阶段 O(HashSize)
    Point best_point = {0, 0};
    int64_t min_dist_sq = INT64_MAX;
    bool found = false;

    for (const auto& entry : hit_map.table) {
        if (entry.key != FlatIntersectionMap::EMPTY && entry.count == num_objects) {
            Point p = unpack(entry.key);
            int32_t dx = p.x - anchor.x;
            int32_t dy = p.y - anchor.y;
            int64_t d2 = static_cast<int64_t>(dx)*dx + static_cast<int64_t>(dy)*dy;

            if (d2 < min_dist_sq) {
                min_dist_sq = d2;
                best_point = p;
                found = true;
            }
        }
    }

    return found ? std::optional<Point>{best_point} : std::nullopt;
}

int main() {
    std::vector<Point> all_points;
    std::vector<size_t> offsets;

    // --- 数据生成 ---

    // 对象 1: y = x (范围 -1000 到 1000)
    offsets.push_back(all_points.size());
    for (int16_t i = -1000; i <= 1000; ++i) {
        all_points.push_back({i, i});
    }

    // 对象 2: y = x^2
    // 注意: int16 极大值为 32767, 所以 x 最大到 sqrt(32767) ≈ 181
    offsets.push_back(all_points.size());
    for (int16_t i = -181; i <= 181; ++i) {
        all_points.push_back({i, static_cast<int16_t>(i * i)});
    }

    // --- 测试场景 1: 锚点靠近 (1,1) ---
    Point anchor1 = {2, 2};
    auto res1 = find_nearest_optimized(all_points, offsets, anchor1);

    // --- 测试场景 2: 锚点靠近 (0,0) ---
    Point anchor2 = {-1, -1};
    auto res2 = find_nearest_optimized(all_points, offsets, anchor2);

    // --- 性能基准测试 ---
    auto t1 = std::chrono::high_resolution_clock::now();
    for(int i=0; i<10000; ++i) {
        find_nearest_optimized(all_points, offsets, anchor1);
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    // --- 结果输出 ---
    std::cout << "Scenario 1 (Anchor {2,2}): Nearest Intersection -> ("
              << (res1 ? std::to_string(res1->x) : "None") << ","
              << (res1 ? std::to_string(res1->y) : "None") << ")\n";

    std::cout << "Scenario 2 (Anchor {-1,-1}): Nearest Intersection -> ("
              << (res2 ? std::to_string(res2->x) : "None") << ","
              << (res2 ? std::to_string(res2->y) : "None") << ")\n";

    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::cout << "\nBenchmark (10,000 runs):" << std::endl;
    std::cout << "Total time: " << total_us / 1000.0 << " ms" << std::endl;
    std::cout << "Average time per run: " << total_us / 10000.0 << " us" << std::endl;

    return 0;
}