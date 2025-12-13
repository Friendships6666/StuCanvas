// --- 文件路径: src/main.cpp ---

#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h" // 必须包含 SetIndustryStageCallback 和 UpdateTargetViewState

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

// 最终点数据缓冲区 (直接映射到 JS SharedArrayBuffer)
AlignedVector<PointData> wasm_final_contiguous_buffer;
AlignedVector<FunctionRange> wasm_function_ranges_buffer;

// 状态标记 (仅供调试查询，不再用于控制核心逻辑)
std::atomic<bool> g_is_calculating{false};

// TBB 任务组指针 (★ 仅限后台线程访问/重置 ★)
std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

constexpr size_t INITIAL_BUFFER_CAPACITY = 200000;

// =========================================================
//        ↓↓↓ 任务管理器 (单常驻线程 + Watchdog控制) ↓↓↓
// =========================================================

struct CalculationRequest {
    std::vector<std::string> implicit_rpn_list;       // 对应 Pairs (中缀转RPN)
    std::vector<std::string> implicit_rpn_direct_list; // 对应 Direct (直接RPN)
    std::vector<std::string> industry_rpn_list;
    double offset_x;
    double offset_y;
    double zoom;
    double screen_width;
    double screen_height;
};

// 前向声明
void calculate_points_internal(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

class CalculationManager {
public:
    CalculationManager() {
        m_running = true;
        m_is_idle = true; // 初始为空闲
        // 启动唯一的常驻后台管理线程
        m_worker_thread = std::thread(&CalculationManager::worker_loop, this);
    }

    ~CalculationManager() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv_request.notify_all();
        if (m_worker_thread.joinable()) {
            m_worker_thread.join();
        }
    }

    // [非阻塞] JS 调用此函数提交新任务
    void submit_task(const CalculationRequest& req) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_request = req; // 覆盖旧请求 (Request Coalescing)
            m_has_request = true;
        }

        // ★★★ 核心 1：更新全局 Watchdog 目标 ★★★
        // 只有主线程（UI线程）有权设置目标。后台线程只能读。
        // 这会让正在运行的 plotIndustry 线程检测到参数不一致，毫秒级内自动 return。
        UpdateTargetViewState(
            req.offset_x,
            req.offset_y,
            req.zoom,
            req.screen_width,
            req.screen_height
        );

        // ★★★ 核心 2：发送 TBB 取消信号 (作为辅助) ★★★
        // 强制打断当前的 wait 或 calculation
        cancel_industry_calculation();

        // 3. 唤醒后台线程处理新请求
        m_cv_request.notify_one();
    }

    // [阻塞] 停止当前所有后台任务，并等待直到后台线程变为空闲
    // 供 calculate_implicit_sync 使用，确保独占访问全局 Buffer
    void cancel_and_wait_for_idle() {
        // 1. 设置一个“不可能”的视图状态，强迫后台任务立即因 Watchdog 校验失败而终止
        UpdateTargetViewState(-9999999.0, -9999999.0, -1.0, 0, 0);
        cancel_industry_calculation();

        // 2. 清除积压的请求
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_request = std::nullopt;
            m_has_request = false;
        }

        // 3. ★★★ 安全等待后台线程变为空闲 ★★★
        // 这里不操作 TBB 对象，只等待 worker_loop 跑到循环末尾
        std::unique_lock<std::mutex> lock(m_idle_mutex);
        m_cv_idle.wait(lock, [this] { return m_is_idle; });
    }

private:
    void worker_loop() {
        std::cout << "[Manager] Worker thread started." << std::endl;

        while (m_running) {
            CalculationRequest req;

            // --- 阶段 1: 等待请求 ---
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv_request.wait(lock, [this] { return m_has_request || !m_running; });

                if (!m_running) break;

                if (m_pending_request) {
                    req = *m_pending_request;
                    m_pending_request = std::nullopt;
                    m_has_request = false;
                } else {
                    continue; // 虚假唤醒
                }
            }

            // --- 阶段 2: 标记忙碌，准备环境 ---
            {
                std::lock_guard<std::mutex> lock(m_idle_mutex);
                m_is_idle = false;
            }
            g_is_calculating.store(true, std::memory_order_release);

            // 异常捕获：确保线程永不崩溃
            try {
                // 清理旧任务 (仅在此线程操作 task_group，绝对线程安全)
                cancel_industry_calculation();

                if (g_global_task_group) {
                    try { g_global_task_group->wait(); } catch (...) {}
                    g_global_task_group.reset(); // 销毁旧对象
                }
                g_global_task_group = std::make_unique<oneapi::tbb::task_group>();

                // 二次检查请求插队 (优化)
                bool skip = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_has_request) skip = true;
                }

                if (!skip) {
                    // ★★★ 重要：不要在这里调用 UpdateTargetViewState ★★★
                    // 目标状态已经在 submit_task 中由主线程设置了。
                    // 后台线程只负责执行，不负责修改目标。这避免了时序竞争导致的“回滚旧视图”bug。

                    std::vector<std::string> empty_implicit;
                    // 如果需要在异步模式下计算普通隐函数，可以将 req.implicit_rpn_list 传进去
                    // 这里假设异步模式只关注 Industry 函数，或者 req 中包含了需要的数据

                    g_global_task_group->run([=]() {
                        calculate_points_internal(
                            req.implicit_rpn_list, // 传入请求中的列表
                            req.implicit_rpn_direct_list,
                            req.industry_rpn_list,
                            req.offset_x, req.offset_y,
                            req.zoom,
                            req.screen_width, req.screen_height
                        );
                    });

                    // 等待计算 (它会因为 Watchdog 检查或完成而返回)
                    // 注意：plotIndustry 即使被 cancel 也会 push 一个空结果，解除 plotCall 中的阻塞，从而让这里的 wait 返回
                    g_global_task_group->wait();
                }

            } catch (const std::exception& e) {
                std::cerr << "[Manager] Exception in worker: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[Manager] Unknown Exception in worker." << std::endl;
            }

            // --- 阶段 3: 标记空闲，通知等待者 ---
            g_is_calculating.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(m_idle_mutex);
                m_is_idle = true;
            }
            m_cv_idle.notify_all(); // 唤醒可能在等待的 cancel_and_wait_for_idle
        }
        std::cout << "[Manager] Worker thread exiting." << std::endl;
    }

    std::thread m_worker_thread;

    // 请求队列控制
    std::mutex m_mutex;
    std::condition_variable m_cv_request;
    std::optional<CalculationRequest> m_pending_request;
    bool m_has_request = false;

    // 空闲状态控制
    std::mutex m_idle_mutex;
    std::condition_variable m_cv_idle;
    bool m_is_idle = true;

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
    const std::vector<std::string>& implicit_rpn_list,        // Pairs
    const std::vector<std::string>& implicit_rpn_direct_list, // Direct RPN
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    // 1. 构建 Pairs 列表
    std::vector<std::pair<std::string, std::string>> implicit_rpn_pairs;
    implicit_rpn_pairs.reserve(implicit_rpn_list.size());
    for (const auto& rpn_str : implicit_rpn_list) {
        implicit_rpn_pairs.push_back({rpn_str, rpn_str});
    }

    bool has_industry = !industry_rpn_list.empty();

    if (has_industry) {
        // --- 工业模式 ---
        // 工业点由 plotIndustry 直接写入 wasm_final_contiguous_buffer 并触发回调。

        if (wasm_final_contiguous_buffer.capacity() < INITIAL_BUFFER_CAPACITY) {
            wasm_final_contiguous_buffer.reserve(INITIAL_BUFFER_CAPACITY);
        }

        // 准备普通隐函数的缓冲区
        AlignedVector<PointData> ordered_points;
        if (!implicit_rpn_pairs.empty() || !implicit_rpn_direct_list.empty()) {
            ordered_points.reserve(INITIAL_BUFFER_CAPACITY / 2);
        }

        // 调用核心计算
        // 这里的 zoom 参数会一路传到 execute_native_solver 用于 Watchdog 校验
        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            implicit_rpn_direct_list,
            industry_rpn_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 如果有普通隐函数，追加合并
        size_t industry_count = wasm_final_contiguous_buffer.size();
        size_t implicit_count = ordered_points.size();

        // Ranges 索引偏移：Pairs + Direct
        size_t industry_start_idx = implicit_rpn_pairs.size() + implicit_rpn_direct_list.size();

        if (wasm_function_ranges_buffer.size() <= industry_start_idx) {
            // 注意：calculate_points_core 可能已经 resize 了，这里只是兜底
            wasm_function_ranges_buffer.resize(industry_start_idx + industry_rpn_list.size());
        }

        if (implicit_count > 0) {
            size_t total_needed = industry_count + implicit_count;
            if (wasm_final_contiguous_buffer.capacity() < total_needed) {
                wasm_final_contiguous_buffer.reserve(total_needed);
            }
            // 将普通点追加到 Buffer 后面
            wasm_final_contiguous_buffer.insert(
                wasm_final_contiguous_buffer.end(),
                ordered_points.begin(),
                ordered_points.end()
            );

            // 修正普通函数的 Range (因为它们被排在 Industry 后面)
            // 注意：这取决于 calculate_points_core 的逻辑，
            // 假设 core 逻辑是 先排 implicit 再排 industry，
            // 但物理存储上，Industry 先占了位置（因为 assign），Implicit 后追加。
            // 所以 Implicit 的 Range 需要加上 Industry 的数量偏移。
            for(size_t i=0; i<industry_start_idx; ++i) {
                wasm_function_ranges_buffer[i].start_index += (uint32_t)industry_count;
            }
        }
    }
    else {
        // --- 纯隐函数模式 ---
        AlignedVector<PointData> ordered_points;
        ordered_points.reserve(INITIAL_BUFFER_CAPACITY);
        std::vector<std::string> empty_industry;

        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            implicit_rpn_direct_list,
            empty_industry,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 直接覆盖
        wasm_final_contiguous_buffer.assign(ordered_points.begin(), ordered_points.end());
    }
}

// =========================================================
//        ↓↓↓ WASM 绑定接口 ↓↓↓
// =========================================================
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/threading.h> // ★ 关键头文件

// 全局保存 JS 回调对象
emscripten::val g_js_update_callback = emscripten::val::null();

// 辅助函数：在主线程执行回调
void safe_js_callback_dispatch(void*) {
    if (!g_js_update_callback.isNull()) {
        g_js_update_callback(); // 触发 JS 的 onStageUpdate
    }
}

// 注册回调函数
void set_js_callback(emscripten::val callback) {
    g_js_update_callback = callback;

    SetIndustryStageCallback([]() {
        // 将回调 Dispatch 到主线程执行，避免后台线程直接操作 JS 对象
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, safe_js_callback_dispatch, nullptr);
    });
}

// 同步计算接口
void calculate_implicit_sync(
    const std::vector<std::string>& implicit_rpn_list,        // Pairs
    const std::vector<std::string>& implicit_rpn_direct_list, // Direct RPN
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();

    // ★★★ 安全等待后台空闲 ★★★
    // 这会挂起主线程，直到后台线程收到 Cancel 信号并退出当前计算
    g_calc_manager->cancel_and_wait_for_idle();

    g_is_calculating.store(false);

    std::vector<std::string> empty_industry;
    calculate_points_internal(
        implicit_rpn_list,
        implicit_rpn_direct_list,
        empty_industry,
        offset_x, offset_y, zoom, screen_width, screen_height
    );
}

// 异步计算接口
void start_industry_async(
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();

    CalculationRequest req;
    req.industry_rpn_list = industry_rpn_list;
    // 异步任务这里暂不处理 implicit，如果需要混合，可扩展 JS 接口传入
    req.offset_x = offset_x;
    req.offset_y = offset_y;
    req.zoom = zoom;
    req.screen_width = screen_width;
    req.screen_height = screen_height;

    // 提交任务给后台线程 (非阻塞)
    g_calc_manager->submit_task(req);
}

// 占位函数 (保持接口兼容)
void cancel_calculation_wrapper() {
    cancel_industry_calculation();
}

// Getters
bool is_calculating() { return g_is_calculating.load(std::memory_order_acquire); }
uintptr_t get_points_ptr() { return reinterpret_cast<uintptr_t>(wasm_final_contiguous_buffer.data()); }
size_t get_points_size() { return wasm_final_contiguous_buffer.size(); }
uintptr_t get_function_ranges_ptr() { return reinterpret_cast<uintptr_t>(wasm_function_ranges_buffer.data()); }
size_t get_function_ranges_size() { return wasm_function_ranges_buffer.size(); }

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::register_vector<std::string>("VectorString");

    emscripten::function("set_js_callback", &set_js_callback);
    emscripten::function("calculate_implicit_sync", &calculate_implicit_sync);
    emscripten::function("start_industry_async", &start_industry_async);
    emscripten::function("cancel_calculation", &cancel_calculation_wrapper);
    emscripten::function("is_calculating", &is_calculating);
    emscripten::function("get_points_ptr", &get_points_ptr);
    emscripten::function("get_points_size", &get_points_size);
    emscripten::function("get_function_ranges_ptr", &get_function_ranges_ptr);
    emscripten::function("get_function_ranges_size", &get_function_ranges_size);
}

#else

// =========================================================
//        ↓↓↓ Native EXE 逻辑 (Stub) ↓↓↓
// =========================================================

std::pair<std::vector<PointData>, std::vector<FunctionRange>> calculate_points_for_native(
    const std::vector<std::string>& implicit_rpn,
    const std::vector<std::string>& industry_rpn_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {



    std::vector<std::string> empty_direct;
    calculate_points_internal(empty_direct, implicit_rpn, industry_rpn_list, offset_x, offset_y, zoom, screen_width, screen_height);
    return {
        std::vector<PointData>(wasm_final_contiguous_buffer.begin(), wasm_final_contiguous_buffer.end()),
        std::vector<FunctionRange>(wasm_function_ranges_buffer.begin(), wasm_function_ranges_buffer.end())
    };
}

int main() {
    try {
        std::vector<std::string> implicit_rpn = {"x 2 pow y 2 pow + 10 -"};
        std::vector<std::string> industry_rpn = {  };

        double offset_x = 0, offset_y = 0;
        double zoom = 0.1;
        double screen_width = 2560, screen_height = 1600;

        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;

        // 模拟回调设置
        SetIndustryStageCallback([](){
            std::cout << "[Callback] Stage Updated! Points: " << wasm_final_contiguous_buffer.size() << std::endl;
        });

        // 模拟更新视图目标
        UpdateTargetViewState(offset_x, offset_y, zoom, screen_width, screen_height);

        auto start_time = std::chrono::high_resolution_clock::now();

        auto results = calculate_points_for_native(
            implicit_rpn,
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