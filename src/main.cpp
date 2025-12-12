// --- 文件路径: src/main.cpp ---

#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <optional>
#include <vector>

// =========================================================
//        ↓↓↓ 全局变量定义 ↓↓↓
// =========================================================

AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

std::atomic<int> g_industry_stage_version{0};
std::atomic<bool> g_is_calculating{false};
std::atomic<size_t> g_points_atomic_index{0};

std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

constexpr size_t INITIAL_BUFFER_CAPACITY = 200000;

// =========================================================
//        ↓↓↓ 任务管理器 (单常驻线程模型) ↓↓↓
// =========================================================
// 解决 "Thread pool exhausted" 问题的核心类

struct CalculationRequest {
    std::vector<std::string> industry_rpn_list;
    double offset_x;
    double offset_y;
    double zoom;
    double screen_width;
    double screen_height;
};

// 前向声明核心计算函数
void calculate_points_internal(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

class CalculationManager {
public:
    CalculationManager() {
        // 构造时启动唯一的常驻后台线程
        m_running = true;
        m_worker_thread = std::thread(&CalculationManager::worker_loop, this);
    }

    ~CalculationManager() {
        // 析构逻辑（虽然在WASM中通常不退出，但为了规范）
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all();
        if (m_worker_thread.joinable()) {
            m_worker_thread.join();
        }
    }

    // JS 调用此函数提交新任务 (非阻塞)
    void submit_task(const CalculationRequest& req) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // 覆盖旧请求，实现“请求合并”，只处理最新的拖动位置
            m_pending_request = req;
            m_has_request = true;
        }
        // 唤醒后台线程
        m_cv.notify_one();
    }

    // 停止当前所有后台任务（给同步计算 calculate_implicit_sync 使用）
    // 这是一个阻塞调用，直到后台任务彻底停下
    void cancel_and_wait() {
        // 1. 发送取消信号给 TBB (Context Cancellation)
        cancel_industry_calculation();

        // 2. 清空待处理请求，防止后台线程刚醒来又开始算新的
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_request = std::nullopt;
            m_has_request = false;
        }

        // 3. 等待当前正在跑的 TBB 任务结束
        // 注意：我们只等待任务组清空，不干扰 worker_thread 的运行循环
        if (g_global_task_group) {
            g_global_task_group->wait();
        }
    }

private:
    // 这是在独立线程中运行的循环
    void worker_loop() {
        while (m_running) {
            CalculationRequest req;

            // 1. 等待新请求
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_has_request || !m_running; });

                if (!m_running) break;

                if (m_pending_request) {
                    req = *m_pending_request;
                    m_pending_request = std::nullopt;
                    m_has_request = false;
                } else {
                    // 虚假唤醒或请求被抢占
                    continue;
                }
            }

            // 2. 在开始新任务前，先清理上一轮可能还在跑的任务
            // 即使是从 wait() 醒来，也要确保之前的 TBB 任务组已彻底完成/取消
            cancel_industry_calculation();

            if (g_global_task_group) {
                g_global_task_group->wait();
            } else {
                g_global_task_group = std::make_unique<oneapi::tbb::task_group>();
            }

            // 二次检查：在清理过程中，如果 JS 又发来了更新的请求，
            // 那么当前的 req 已经过时了，直接跳过，去取最新的。
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_has_request) continue;
            }

            // 3. 准备开始计算
            // 重置状态：版本归零，标记计算中。
            // 注意：我们不清空 buffer，保留旧画面以实现平滑过渡。
            g_industry_stage_version.store(0, std::memory_order_release);
            g_is_calculating.store(true, std::memory_order_release);

            std::vector<std::string> empty_implicit;

            // 4. 启动 TBB 任务
            // 我们使用 run 来让 TBB 调度器接管，但紧接着会在本线程 wait。
            g_global_task_group->run([=]() {
                calculate_points_internal(
                    empty_implicit,
                    req.industry_rpn_list,
                    req.offset_x, req.offset_y,
                    req.zoom,
                    req.screen_width, req.screen_height
                );
            });

            // 5. 等待计算完成
            // 期间如果 calculate_implicit_sync 调用了 cancel_industry_calculation，
            // 或者 submit_task 触发了下一轮循环的 cancel，
            // 这个 wait() 会提前返回。
            g_global_task_group->wait();

            // 6. 标记结束
            // 注意：如果是因为被 cancel 而结束，这里也会标记 false，
            // 但下一轮循环开始时会立刻重置状态，所以 UI 不会受影响。
            g_is_calculating.store(false, std::memory_order_release);
            g_industry_stage_version.fetch_add(1, std::memory_order_release);
        }
    }

    std::thread m_worker_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<CalculationRequest> m_pending_request;
    bool m_has_request = false;
    bool m_running = false;
};

// 全局单例管理器
static std::unique_ptr<CalculationManager> g_calc_manager;

void ensure_manager() {
    if (!g_calc_manager) {
        g_calc_manager = std::make_unique<CalculationManager>();
    }
}

// =========================================================
//        ↓↓↓ 核心计算逻辑 ↓↓↓
// =========================================================

void calculate_points_internal(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    bool has_industry = !industry_rpn_list.empty();

    if (has_industry) {
        // 工业模式：数据追加逻辑
        if (wasm_final_contiguous_buffer.capacity() < INITIAL_BUFFER_CAPACITY) {
            wasm_final_contiguous_buffer.reserve(INITIAL_BUFFER_CAPACITY);
        }

        AlignedVector<PointData> ordered_points;
        if (!implicit_rpn_pairs.empty()) {
            ordered_points.reserve(INITIAL_BUFFER_CAPACITY / 2);
        }

        // 工业函数内部直接写入 wasm_final_contiguous_buffer
        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            industry_rpn_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 合并普通隐函数点
        size_t industry_count = wasm_final_contiguous_buffer.size();
        size_t implicit_count = ordered_points.size();
        size_t total_needed = industry_count + implicit_count;

        size_t industry_start_idx = implicit_rpn_pairs.size();
        if (wasm_function_ranges_buffer.size() <= industry_start_idx) {
            wasm_function_ranges_buffer.resize(industry_start_idx + 1);
        }

        if (implicit_count > 0) {
            if (wasm_final_contiguous_buffer.capacity() < total_needed) {
                wasm_final_contiguous_buffer.reserve(total_needed);
            }
            wasm_final_contiguous_buffer.insert(
                wasm_final_contiguous_buffer.end(),
                ordered_points.begin(),
                ordered_points.end()
            );

            // 修正 Range 偏移
            for(size_t i=0; i<industry_start_idx; ++i) {
                wasm_function_ranges_buffer[i].start_index += (uint32_t)industry_count;
            }
        }
    }
    else {
        // 纯隐函数模式：直接赋值
        AlignedVector<PointData> ordered_points;
        ordered_points.reserve(INITIAL_BUFFER_CAPACITY);
        std::vector<std::string> empty_industry;

        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            empty_industry,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        wasm_final_contiguous_buffer.assign(ordered_points.begin(), ordered_points.end());
    }
}

// =========================================================
//        ↓↓↓ WASM 绑定接口 ↓↓↓
// =========================================================
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>

void calculate_implicit_sync(
    const std::vector<std::string>& implicit_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();

    // 1. 强制停止后台所有任务，并等待它们清理完毕
    // 这解决了 JS Worker 被 wait() 死锁的问题，因为 cancel_industry_calculation 会被调用
    g_calc_manager->cancel_and_wait();

    // 2. 此时后台线程处于等待信号状态，buffer 安全
    g_is_calculating.store(false);

    // 3. 执行同步计算
    std::vector<std::string> empty_industry;
    calculate_points_internal(implicit_rpn_list, empty_industry, offset_x, offset_y, zoom, screen_width, screen_height);
}

void start_industry_async(
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();

    // 提交任务给常驻后台线程
    // 这是一个极快的非阻塞操作 (加锁->拷贝->通知)
    // 解决了 "Thread pool exhausted" 问题
    CalculationRequest req;
    req.industry_rpn_list = industry_rpn_list;
    req.offset_x = offset_x;
    req.offset_y = offset_y;
    req.zoom = zoom;
    req.screen_width = screen_width;
    req.screen_height = screen_height;

    g_calc_manager->submit_task(req);
}

void cancel_calculation_wrapper() {
    cancel_industry_calculation();
}

int get_data_version() { return g_industry_stage_version.load(std::memory_order_acquire); }
bool is_calculating() { return g_is_calculating.load(std::memory_order_acquire); }
uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");
    emscripten::function("calculate_implicit_sync", &calculate_implicit_sync);
    emscripten::function("start_industry_async", &start_industry_async);
    emscripten::function("get_data_version", &get_data_version);
    emscripten::function("is_calculating", &is_calculating);
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
    emscripten::function("cancel_calculation", &cancel_calculation_wrapper);
}

#else

// =========================================================
//        ↓↓↓ Native EXE 逻辑 ↓↓↓
// =========================================================

std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::pair<std::string, std::string>>& implicit_rpn_pairs,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    std::vector<std::string> implicit_strs;
    implicit_strs.reserve(implicit_rpn_pairs.size());
    for(const auto& p : implicit_rpn_pairs) {
        implicit_strs.push_back(p.first);
    }
    calculate_points_internal(implicit_strs, industry_rpn_list, offset_x, offset_y, zoom, screen_width, screen_height);
    return {
        std::vector<PointData>(wasm_final_contiguous_buffer.begin(), wasm_final_contiguous_buffer.end()),
        std::vector<FunctionRange>(wasm_function_ranges_buffer.begin(), wasm_function_ranges_buffer.end())
    };
}

int main() {
    try {
        std::vector<std::pair<std::string, std::string>> all_implicit_rpn_pairs;
        std::vector<std::string> industry_rpn = { "y x tan -;0;0.1;2;2" };

        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 2560, screen_height = 1600;

        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        auto results = calculate_points_for_native(
            all_implicit_rpn_pairs,
            industry_rpn,
            offset_x, offset_y, zoom, screen_width, screen_height
        );
        const auto& final_points = results.first;

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---\n";
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总共生成了 " << final_points.size() << " 个点。" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#endif // __EMSCRIPTEN__