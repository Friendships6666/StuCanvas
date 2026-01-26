#include <iostream>
#include <vector>
#include <limits>
#include <chrono>
#include <span>
#include <algorithm>
#include <random>
#include <iomanip>

struct Point {
    int16_t x, y;
};

const int16_t SENTINEL = std::numeric_limits<int16_t>::min();

template <typename T>
void do_not_optimize(T const& val) {
    #if defined(__GNUC__) || defined(__clang__)
        asm volatile("" : : "g"(val) : "memory");
    #else
        static volatile T dummy;
        dummy = val;
    #endif
}

inline int32_t distSqInt(Point p1, Point p2) {
    int32_t dx = static_cast<int32_t>(p1.x) - p2.x;
    int32_t dy = static_cast<int32_t>(p1.y) - p2.y;
    return dx * dx + dy * dy;
}

void run_test(size_t total_points) {
    const Point guess = {1000, 1000};
    std::mt19937 gen(42);
    std::uniform_int_distribution<int16_t> dist(-30000, 30000);

    // 1. 准备包含垃圾数据的原始 Buffer
    std::vector<Point> base_buffer(total_points);
    for (auto& p : base_buffer) p = {dist(gen), dist(gen)};

    struct Range { size_t start; size_t end; };
    std::vector<Range> garbage_ranges = {
        {total_points/100, total_points/50},         // 1% 垃圾
        {total_points/2, total_points/2 + total_points/20}, // 5% 垃圾
        {total_points - total_points/10, total_points - total_points/12} // ~2% 垃圾
    };

    size_t garbage_count = 0;
    for (const auto& r : garbage_ranges) {
        garbage_count += (r.end - r.start);
        for (size_t i = r.start; i < r.end; ++i) base_buffer[i].x = SENTINEL;
    }

    // 有效点总数
    const size_t valid_count = total_points - garbage_count;

    // A. 准备有效 Span
    std::vector<std::span<Point>> valid_spans;
    size_t current = 0;
    for (const auto& r : garbage_ranges) {
        if (current < r.start) valid_spans.push_back({&base_buffer[current], r.start - current});
        current = r.end;
    }
    if (current < total_points) valid_spans.push_back({&base_buffer[current], total_points - current});

    // C. 准备紧凑数据 (大小为 valid_count)
    std::vector<Point> compacted;
    compacted.reserve(valid_count);
    for (auto s : valid_spans) compacted.insert(compacted.end(), s.begin(), s.end());

    // D. 准备严谨对照组 (大小精确为 valid_count，完全无哨兵)
    std::vector<Point> pure_control(valid_count);
    for (auto& p : pure_control) p = {dist(gen), dist(gen)};

    std::cout << "--- 原始总数: " << total_points / 1000000 << "M | 有效总数: "
              << (double)valid_count / 1000000 << "M ---" << std::endl;

    // --- 开始基准测试 ---

    // Test D: 纯净对照组
    {
        auto start = std::chrono::high_resolution_clock::now();
        int32_t min_d = std::numeric_limits<int32_t>::max();
        Point best_p;
        for (const auto& p : pure_control) {
            int32_t d = distSqInt(p, guess);
            if (d < min_d) { min_d = d; best_p = p; }
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(best_p);
        std::cout << "D. 纯净对照组 (无垃圾/无Check): " << std::fixed << std::setprecision(4)
                  << std::chrono::duration<double, std::milli>(end - start).count() << " ms\n";
    }

    // Test A: 分段 Span
    {
        auto start = std::chrono::high_resolution_clock::now();
        int32_t min_d = std::numeric_limits<int32_t>::max();
        Point best_p;
        for (auto s : valid_spans) {
            for (const auto& p : s) {
                int32_t d = distSqInt(p, guess);
                if (d < min_d) { min_d = d; best_p = p; }
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(best_p);
        std::cout << "A. 分段 Span 法 (无Check):        "
                  << std::chrono::duration<double, std::milli>(end - start).count() << " ms\n";
    }

    // Test B: Sentinel If
    {
        auto start = std::chrono::high_resolution_clock::now();
        int32_t min_d = std::numeric_limits<int32_t>::max();
        Point best_p;
        for (const auto& p : base_buffer) {
            if (p.x == SENTINEL) [[unlikely]] continue;
            int32_t d = distSqInt(p, guess);
            if (d < min_d) { min_d = d; best_p = p; }
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(best_p);
        std::cout << "B. Sentinel If 法 (遍历全量):     "
                  << std::chrono::duration<double, std::milli>(end - start).count() << " ms\n";
    }

    // Test C: 完全紧凑法
    {
        auto start = std::chrono::high_resolution_clock::now();
        int32_t min_d = std::numeric_limits<int32_t>::max();
        Point best_p;
        for (const auto& p : compacted) {
            int32_t d = distSqInt(p, guess);
            if (d < min_d) { min_d = d; best_p = p; }
        }
        auto end = std::chrono::high_resolution_clock::now();
        do_not_optimize(best_p);
        std::cout << "C. 完全紧凑法 (无Check):          "
                  << std::chrono::duration<double, std::milli>(end - start).count() << " ms\n\n";
    }
}

int main() {
    run_test(1'000'000);
    run_test(10'000'000);
    run_test(100'000'000);
    return 0;
}