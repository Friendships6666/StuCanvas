#include "../../include/plot/plotIndustry.h"
#include "../../include/CAS/RPN/RPN.h"
#include "../../include/interval/interval.h"
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/mutex.h>

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

// 模板化的核心处理函数
template<typename T>
void execute_industry_processing(
    std::vector<PointData>& out_points, // 最终输出容器
    const IndustrialRPN& rpn,
    unsigned int func_idx,
    const Vec2& world_origin,
    double wppx, double wppy,
    double screen_width, double screen_height
) {
    // ====================================================================
    //   阶段 1: 四叉树细分 (保持不变)
    // ====================================================================
    std::vector<IndustryQuadtreeTaskT<T>> leaf_nodes;
    std::stack<IndustryQuadtreeTaskT<T>> tasks;
    tasks.push({
        T(world_origin.x), T(world_origin.y),
        T(screen_width * wppx), T(screen_height * wppy)
    });
    const T min_world_width = T(wppx);
    while (!tasks.empty()) {
        IndustryQuadtreeTaskT<T> task = tasks.top(); tasks.pop();
        Interval<T> x_interval(task.world_x, task.world_x + task.world_w);
        Interval<T> y_interval(task.world_y + task.world_h, task.world_y);
        Interval<T> result = evaluate_rpn<Interval<T>>(rpn.program, x_interval, y_interval, std::nullopt, rpn.precision_bits);
        if (result.max >= T(0.0) && result.min <= T(0.0)) {
            if (task.world_w <= min_world_width) {
                leaf_nodes.push_back(task);
            } else {
                T w_half = task.world_w / T(2.0); T h_half = task.world_h / T(2.0);
                tasks.push({task.world_x, task.world_y, w_half, h_half});
                tasks.push({task.world_x + w_half, task.world_y, w_half, h_half});
                tasks.push({task.world_x, task.world_y + h_half, w_half, h_half});
                tasks.push({task.world_x + w_half, task.world_y + h_half, w_half, h_half});
            }
        }
    }

    // ====================================================================
    //   ↓↓↓ 阶段 2: 并行批处理与筛选 (性能与内存最优) ↓↓↓
    // ====================================================================
    const int DETAIL_LEVEL = 50; // 细分程度
    const int CHUNK_SIZE = 10;   // 最小处理单元大小

    using PixelCoord = std::pair<int, int>;
    using PointList = std::vector<PointData>;
    using PixelData = std::pair<tbb::mutex, PointList>;
    tbb::concurrent_hash_map<PixelCoord, PixelData, PixelCoordHash> pixel_grid;

    oneapi::tbb::parallel_for_each(leaf_nodes.begin(), leaf_nodes.end(),
        [&](const IndustryQuadtreeTaskT<T>& leaf) {

            // 1. 创建一个生命周期极短的局部容器，用于暂存一个CHUNK的点
            std::vector<PointData> local_chunk_points;
            local_chunk_points.reserve(CHUNK_SIZE * CHUNK_SIZE);

            const T micro_w = leaf.world_w / T(DETAIL_LEVEL);
            const T micro_h = leaf.world_h / T(DETAIL_LEVEL);

            // 2. 以 CHUNK_SIZE 为步长，遍历整个 leaf_node 区域
            for (int j_chunk = 0; j_chunk < DETAIL_LEVEL; j_chunk += CHUNK_SIZE) {
                for (int i_chunk = 0; i_chunk < DETAIL_LEVEL; i_chunk += CHUNK_SIZE) {

                    local_chunk_points.clear(); // 为下一个块重置局部容器

                    // 3. 在 CHUNK_SIZE x CHUNK_SIZE 的单元内计算，并将点存入局部容器
                    for (int j = j_chunk; j < j_chunk + CHUNK_SIZE; ++j) {
                        for (int i = i_chunk; i < i_chunk + CHUNK_SIZE; ++i) {
                            T current_x = leaf.world_x + T(i) * micro_w;
                            T current_y = leaf.world_y + T(j) * micro_h;
                            Interval<T> micro_x_interval(current_x, current_x + micro_w);
                            Interval<T> micro_y_interval(current_y + micro_h, current_y);
                            Interval<T> micro_result = evaluate_rpn<Interval<T>>(rpn.program, micro_x_interval, micro_y_interval, std::nullopt, rpn.precision_bits);

                            if (micro_result.max >= T(0.0) && micro_result.min <= T(0.0)) {
                                T center_x_world = current_x + micro_w / T(2.0);
                                T center_y_world = current_y + micro_h / T(2.0);
                                T screen_x = (center_x_world - T(world_origin.x)) / T(wppx);
                                T screen_y = (center_y_world - T(world_origin.y)) / T(wppy);
                                local_chunk_points.emplace_back(PointData{{ convert_to_double(screen_x), convert_to_double(screen_y) }, func_idx});
                            }
                        }
                    }

                    // 4. 如果这个块产生了点，则对这批点进行筛选
                    if (!local_chunk_points.empty()) {
                        for (const auto& new_point : local_chunk_points) {
                            PixelCoord coord = { static_cast<int>(new_point.position.x), static_cast<int>(new_point.position.y) };
                            tbb::concurrent_hash_map<PixelCoord, PixelData, PixelCoordHash>::accessor acc;
                            pixel_grid.insert(acc, coord);
                            tbb::mutex::scoped_lock lock(acc->second.first);
                            PointList& points = acc->second.second;

                            if (points.size() < 2) {
                                points.push_back(new_point);
                            } else {
                                double sum_new = new_point.position.x + new_point.position.y;
                                double sum0 = points[0].position.x + points[0].position.y;
                                double sum1 = points[1].position.x + points[1].position.y;
                                if (sum0 > sum1) {
                                    std::swap(points[0], points[1]);
                                    std::swap(sum0, sum1);
                                }
                                if (sum_new < sum0) {
                                    points[0] = new_point;
                                } else if (sum_new > sum1) {
                                    points[1] = new_point;
                                }
                            }
                        }
                    }
                    // 局部容器 local_chunk_points 在此块处理完后，内容被清空，内存被复用
                }
            }
        }
    );

    // ====================================================================
    //   阶段 3: 结果复制 (保持不变)
    // ====================================================================
    out_points.reserve(pixel_grid.size() * 2);
    for (const auto& pair : pixel_grid) {
        for (const auto& point : pair.second.second) {
            out_points.push_back(point);
        }
    }
}

} // 匿名命名空间结束

// 公共接口函数 (保持不变)
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