// --- 文件路径: src/plot/plotIndustry.cpp ---

#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/mutex.h>
#include <atomic> // 用于线程安全的原子操作
#include <print>  // 用于 C++23 线程安全的打印

namespace { // 使用匿名命名空间封装模板化实现细节

// ... [PixelCoordHash, IndustryQuadtreeTaskT, convert_to_double 保持不变] ...
struct PixelCoordHash {
    static size_t hash(const std::pair<int, int>& p) {
        std::hash<int> hasher;
        size_t h1 = hasher(p.first);
        size_t h2 = hasher(p.second);
        return h1 ^ (h2 << 1);
    }
    static bool equal(const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return a.first == b.first && a.second == b.second;
    }
};

template<typename T>
struct IndustryQuadtreeTaskT {
    T world_x, world_y;
    T world_w, world_h;
};

template<typename T>
double convert_to_double(const T& val) {
    if constexpr (std::is_same_v<T, hp_float>) {
        return val.template convert_to<double>();
    } else {
        return val;
    }
}

template<typename T>
void execute_industry_processing(
    std::vector<PointData>& out_points, // 最终输出容器
    const IndustrialRPN& rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height
) {
    using PixelCoord = std::pair<int, int>;
    using PointList = std::vector<PointData>;
    using PixelData = std::pair<tbb::mutex, PointList>;
    // 使用 Hash Map 进行像素级分桶，实现过滤逻辑
    using PixelGrid = tbb::concurrent_hash_map<PixelCoord, PixelData, PixelCoordHash>;
    PixelGrid pixel_grid;

    std::vector<IndustryQuadtreeTaskT<T>> initial_tasks;
    const int num_tiles_xy = 16;
    T initial_tile_w = T(screen_width * wppx) / T(num_tiles_xy);
    T initial_tile_h = T(screen_height * wppy) / T(num_tiles_xy);

    for (int i = 0; i < num_tiles_xy; ++i) {
        for (int j = 0; j < num_tiles_xy; ++j) {
            initial_tasks.push_back({
                T(world_origin.x) + T(i) * initial_tile_w,
                T(world_origin.y) + T(j) * initial_tile_h,
                initial_tile_w,
                initial_tile_h
            });
        }
    }

    std::atomic<size_t> completed_tasks{0};
    const size_t total_tasks = initial_tasks.size();
    std::atomic<int> last_reported_progress{-1};

    std::print("工业函数 {} 进度: 0%\n", func_idx);

    oneapi::tbb::parallel_for_each(initial_tasks.begin(), initial_tasks.end(),
        [&](const IndustryQuadtreeTaskT<T>& initial_task) {

            std::stack<IndustryQuadtreeTaskT<T>> local_tasks;
            local_tasks.push(initial_task);

            const T min_world_width = T(0.5*wppx);
            const int DETAIL_LEVEL = 10;

            while (!local_tasks.empty()) {
                IndustryQuadtreeTaskT<T> task = local_tasks.top();
                local_tasks.pop();

                Interval<T> x_interval(task.world_x, task.world_x + task.world_w);
                Interval<T> y_interval(task.world_y + task.world_h, task.world_y);
                Interval<T> result = evaluate_rpn<Interval<T>>(rpn.program, x_interval, y_interval, std::nullopt, rpn.precision_bits);

                if (result.max >= T(0.0) && result.min <= T(0.0)) {
                    if (task.world_w <= min_world_width) {
                        const T micro_w = task.world_w / T(DETAIL_LEVEL);
                        const T micro_h = task.world_h / T(DETAIL_LEVEL);

                        for (int j = 0; j < DETAIL_LEVEL; ++j) {
                            for (int i = 0; i < DETAIL_LEVEL; ++i) {
                                T current_x = task.world_x + T(i) * micro_w;
                                T current_y = task.world_y + T(j) * micro_h;
                                Interval<T> micro_x_interval(current_x, current_x + micro_w);
                                Interval<T> micro_y_interval(current_y + micro_h, current_y);
                                Interval<T> micro_result = evaluate_rpn<Interval<T>>(rpn.program, micro_x_interval, micro_y_interval, std::nullopt, rpn.precision_bits);

                                if (micro_result.max >= T(0.0) && micro_result.min <= T(0.0)) {
                                    T center_x_world = current_x + micro_w / T(2.0);
                                    T center_y_world = current_y + micro_h / T(2.0);

                                    // ====================================================================
                                    //   ↓↓↓ [核心修改] ↓↓↓
                                    // --------------------------------------------------------------------
                                    //   1. 计算屏幕坐标 (Screen Coordinates)
                                    //   2. 将屏幕坐标存入 PointData
                                    //   3. 使用整数像素坐标作为 Hash Key 进行过滤 (每个像素最多2个点)
                                    // ====================================================================

                                    // 计算精确的浮点屏幕坐标
                                    double screen_x_val = convert_to_double((center_x_world - T(world_origin.x)) / T(wppx));
                                    double screen_y_val = convert_to_double((center_y_world - T(world_origin.y)) / T(wppy));

                                    PointData new_point{
                                        {screen_x_val, screen_y_val}, // 存储屏幕坐标
                                        func_idx
                                    };

                                    // 计算整数像素坐标，用于分桶
                                    PixelCoord coord = { static_cast<int>(screen_x_val), static_cast<int>(screen_y_val) };

                                    PixelGrid::accessor acc;
                                    pixel_grid.insert(acc, coord);
                                    tbb::mutex::scoped_lock lock(acc->second.first);
                                    PointList& points = acc->second.second;

                                    // --- 过滤逻辑 ---
                                    if (points.size() < 2) {
                                        // 如果当前像素内的点少于2个，直接添加
                                        points.push_back(new_point);
                                    } else {
                                        // 如果已经有2个点，保留分布最广的两个点（基于 x+y 的和）
                                        // 这种启发式方法有助于在像素内保留对角线两端的点，减少视觉聚集
                                        double sum_new = new_point.position.x + new_point.position.y;
                                        double sum0 = points[0].position.x + points[0].position.y;
                                        double sum1 = points[1].position.x + points[1].position.y;

                                        // 确保 points[0] 是较小的 sum，points[1] 是较大的 sum
                                        if (sum0 > sum1) { std::swap(points[0], points[1]); std::swap(sum0, sum1); }

                                        if (sum_new < sum0) points[0] = new_point;      // 替换最小值
                                        else if (sum_new > sum1) points[1] = new_point; // 替换最大值
                                    }
                                }
                            }
                        }
                    } else {
                        T w_half = task.world_w / T(2.0); T h_half = task.world_h / T(2.0);
                        local_tasks.push({task.world_x, task.world_y, w_half, h_half});
                        local_tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                        local_tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                        local_tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
                    }
                }
            } // end while

            size_t current_completed = completed_tasks.fetch_add(1, std::memory_order_relaxed) + 1;
            int current_progress = static_cast<int>((current_completed * 100) / total_tasks);
            int expected_progress = last_reported_progress.load(std::memory_order_relaxed);
            while (current_progress > expected_progress) {
                if (last_reported_progress.compare_exchange_strong(expected_progress, current_progress)) {
                    std::print("工业函数 {} 进度: {}%\n", func_idx, current_progress);
                }
                expected_progress = last_reported_progress.load(std::memory_order_relaxed);
            }
        }
    ); // end parallel_for_each

    if (last_reported_progress.load() < 100) {
        std::print("工业函数 {} 进度: 100%\n", func_idx);
    }

    out_points.reserve(pixel_grid.size() * 2);
    for (const auto& pair : pixel_grid) {
        for (const auto& point : pair.second.second) {
            out_points.push_back(point);
        }
    }
}

} // 匿名命名空间结束

void process_single_industry_function(
    oneapi::tbb::concurrent_bounded_queue<FunctionResult>* results_queue,
    const std::string& industry_rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height,
    double offset_x, double offset_y, double zoom
) {
    std::vector<PointData> local_points;
    local_points.reserve(5000);

    try {
        IndustrialRPN rpn = parse_industrial_rpn(industry_rpn);
        if (rpn.precision_bits == 0) {
            execute_industry_processing<double>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        } else {
            hp_float::default_precision(rpn.precision_bits);
            execute_industry_processing<hp_float>(
                local_points, rpn, func_idx, world_origin, wppx, wppy, screen_width, screen_height
            );
        }
    } catch (const std::exception& e) {
        std::cerr << "处理工业级RPN时出错 '" << industry_rpn << "': " << e.what() << std::endl;
    }

    results_queue->push({func_idx, std::move(local_points)});
}