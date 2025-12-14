// --- 文件路径: src/main.cpp ---

#include "../pch.h"
#include "../include/plot/plotCall.h"
#include "../include/CAS/symbolic/GraphicSimplify.h"
#include "../include/CAS/AST/JsonAdapter.h"
#include "../include/plot/plotIndustry.h" // 包含 SetIndustryStageCallback 和 UpdateTargetViewState

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

// 状态标记
std::atomic<bool> g_is_calculating{false};

// TBB 任务组指针 (★ 仅限后台线程访问/重置 ★)
std::unique_ptr<oneapi::tbb::task_group> g_global_task_group;

constexpr size_t INITIAL_BUFFER_CAPACITY = 200000;

// =========================================================
//        ↓↓↓ 任务管理器 (单常驻线程 + Watchdog控制) ↓↓↓
// =========================================================

struct CalculationRequest {
    std::vector<std::string> implicit_rpn_list;        // Pairs (中缀转RPN)
    std::vector<std::string> implicit_rpn_direct_list; // Direct (直接RPN)
    std::vector<std::string> explicit_rpn_list;        // 普通显函数 (y=f(x))
    std::vector<std::string> explicit_parametric_list; // ★★★ 新增：普通参数方程 (x(t), y(t)) ★★★
    std::vector<std::string> industry_rpn_list;        // 工业级隐函数
    std::vector<std::string> industry_parametric_list; // 工业级参数方程
    double offset_x;
    double offset_y;
    double zoom;
    double screen_width;
    double screen_height;
};

// 前向声明 (注意参数列表更新)
void calculate_points_internal(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& explicit_parametric_list, // <--- 新增参数
    const std::vector<std::string>& industry_rpn_list,
    const std::vector<std::string>& industry_parametric_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
);

class CalculationManager {
public:
    CalculationManager() {
        m_running = true;
        m_is_idle = true;
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
            m_pending_request = req; // Request Coalescing
            m_has_request = true;
        }

        // 更新全局 Watchdog 目标
        UpdateTargetViewState(
            req.offset_x,
            req.offset_y,
            req.zoom,
            req.screen_width,
            req.screen_height
        );

        // 发送 TBB 取消信号
        cancel_industry_calculation();

        // 唤醒后台线程
        m_cv_request.notify_one();
    }

    // [阻塞] 停止当前所有后台任务，并等待直到后台线程变为空闲
    void cancel_and_wait_for_idle() {
        UpdateTargetViewState(-9999999.0, -9999999.0, -1.0, 0, 0);
        cancel_industry_calculation();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending_request = std::nullopt;
            m_has_request = false;
        }

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
                    continue;
                }
            }

            // --- 阶段 2: 执行任务 ---
            {
                std::lock_guard<std::mutex> lock(m_idle_mutex);
                m_is_idle = false;
            }
            g_is_calculating.store(true, std::memory_order_release);

            try {
                cancel_industry_calculation();

                if (g_global_task_group) {
                    try { g_global_task_group->wait(); } catch (...) {}
                    g_global_task_group.reset();
                }
                g_global_task_group = std::make_unique<oneapi::tbb::task_group>();

                bool skip = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_has_request) skip = true;
                }

                if (!skip) {
                    g_global_task_group->run([=]() {
                        calculate_points_internal(
                            req.implicit_rpn_list,
                            req.implicit_rpn_direct_list,
                            req.explicit_rpn_list,
                            req.explicit_parametric_list, // <--- 传入
                            req.industry_rpn_list,
                            req.industry_parametric_list,
                            req.offset_x, req.offset_y,
                            req.zoom,
                            req.screen_width, req.screen_height
                        );
                    });
                    g_global_task_group->wait();
                }

            } catch (const std::exception& e) {
                std::cerr << "[Manager] Exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[Manager] Unknown Exception." << std::endl;
            }

            // --- 阶段 3: 标记空闲 ---
            g_is_calculating.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(m_idle_mutex);
                m_is_idle = true;
            }
            m_cv_idle.notify_all();
        }
    }

    std::thread m_worker_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv_request;
    std::optional<CalculationRequest> m_pending_request;
    bool m_has_request = false;

    std::mutex m_idle_mutex;
    std::condition_variable m_cv_idle;
    bool m_is_idle = true;
    bool m_running = false;
};

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
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& explicit_parametric_list, // <--- 新增参数
    const std::vector<std::string>& industry_rpn_list,
    const std::vector<std::string>& industry_parametric_list,
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

    // 只要有任意一种工业级函数，就走工业模式逻辑 (混合渲染)
    bool has_industry = !industry_rpn_list.empty() || !industry_parametric_list.empty();

    if (has_industry) {
        if (wasm_final_contiguous_buffer.capacity() < INITIAL_BUFFER_CAPACITY) {
            wasm_final_contiguous_buffer.reserve(INITIAL_BUFFER_CAPACITY);
        }

        // 工业模式下，普通函数也需要计算并合并
        AlignedVector<PointData> ordered_points;
        if (!implicit_rpn_pairs.empty() || !implicit_rpn_direct_list.empty() ||
            !explicit_rpn_list.empty() || !explicit_parametric_list.empty()) {
            ordered_points.reserve(INITIAL_BUFFER_CAPACITY / 2);
        }

        // 注意：calculate_points_core 的签名需要同步更新 (在 plotCall.h/.cpp 中)
        // 这里假设 plotCall 已经更新以接受 explicit_parametric_list
        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            implicit_rpn_direct_list,
            explicit_rpn_list,
            explicit_parametric_list, // <--- 传入
            industry_rpn_list,
            industry_parametric_list,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 如果有普通函数点，追加到全局 buffer
        // 工业点通过回调机制独立更新，这里主要服务于普通函数结果的回填
        wasm_final_contiguous_buffer.assign(ordered_points.begin(), ordered_points.end());
    }
    else {
        // --- 纯普通模式 (隐函数 + 显函数 + 参数方程) ---
        AlignedVector<PointData> ordered_points;
        ordered_points.reserve(INITIAL_BUFFER_CAPACITY);
        std::vector<std::string> empty_industry;
        std::vector<std::string> empty_parametric;

        calculate_points_core(
            ordered_points,
            wasm_function_ranges_buffer,
            implicit_rpn_pairs,
            implicit_rpn_direct_list,
            explicit_rpn_list,
            explicit_parametric_list, // <--- 传入
            empty_industry,
            empty_parametric,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        // 直接覆盖全局 Buffer
        wasm_final_contiguous_buffer.assign(ordered_points.begin(), ordered_points.end());
    }
}

// =========================================================
//        ↓↓↓ WASM 绑定接口 ↓↓↓
// =========================================================
#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten/threading.h>

emscripten::val g_js_update_callback = emscripten::val::null();

void safe_js_callback_dispatch(void*) {
    if (!g_js_update_callback.isNull()) {
        g_js_update_callback();
    }
}

void set_js_callback(emscripten::val callback) {
    g_js_update_callback = callback;
    SetIndustryStageCallback([]() {
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, safe_js_callback_dispatch, nullptr);
    });
}

// 同步计算接口 (普通模式)
// 更新接口：增加 explicit_parametric_list
void calculate_implicit_sync(
    const std::vector<std::string>& implicit_rpn_list,
    const std::vector<std::string>& implicit_rpn_direct_list,
    const std::vector<std::string>& explicit_rpn_list,
    const std::vector<std::string>& explicit_parametric_list, // <--- 新增
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();
    g_calc_manager->cancel_and_wait_for_idle(); // 确保独占
    g_is_calculating.store(false);

    std::vector<std::string> empty_industry;
    std::vector<std::string> empty_parametric;
    calculate_points_internal(
        implicit_rpn_list,
        implicit_rpn_direct_list,
        explicit_rpn_list,
        explicit_parametric_list, // Pass
        empty_industry,
        empty_parametric,
        offset_x, offset_y, zoom, screen_width, screen_height
    );
}

// 异步计算接口 (工业模式)
void start_industry_async(
    const std::vector<std::string>& industry_rpn_list,
    const std::vector<std::string>& industry_parametric_list,
    double offset_x, double offset_y,
    double zoom,
    double screen_width, double screen_height
) {
    ensure_manager();
    CalculationRequest req;
    req.industry_rpn_list = industry_rpn_list;
    req.industry_parametric_list = industry_parametric_list;
    // 异步暂不包含普通函数，如需混合可扩展
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
//        ↓↓↓ Native EXE 逻辑 (测试用) ↓↓↓
// =========================================================

int main() {
    try {
        // 1. 普通隐函数
        std::vector<std::string> implicit_rpn = {};

        // 2. 普通显函数测试: y = sin(x) * 10
        // RPN: "x sin 10 *"
        std::vector<std::string> explicit_rpn = {

        };

        // 3. ★★★ 普通参数方程测试 ★★★
        // 蝴蝶曲线 (Butterfly Curve)
        // x = sin(t) * (e^cos(t) - 2cos(4t) - sin(t/12)^5)
        // y = cos(t) * (e^cos(t) - 2cos(4t) - sin(t/12)^5)
        // t 范围 [0, 12pi]

        // 简化版测试: 螺旋线
        // x = t * cos(t)
        // y = t * sin(t)
        // t: [0, 20]

        // 格式: "x_rpn;y_rpn;t_min;t_max"
        std::string spiral_parametric =
            "_t_ _t_ cos *;"  // x = t * cos(t)
            "_t_ _t_ sin *;"  // y = t * sin(t)
            "0;2000";           // t in [0, 20]

        std::vector<std::string> explicit_parametric = {
            spiral_parametric
        };

        // 4. 工业函数 (空)
        std::vector<std::string> industry_rpn = { };
        std::vector<std::string> industry_parametric = { };

        double offset_x = 0, offset_y = 0;
        double zoom = 0.01;
        double screen_width = 2560, screen_height = 1600;

        std::cout << "--- Native EXE: 开始计算... ---" << std::endl;
        ensure_manager();

        auto start_time = std::chrono::high_resolution_clock::now();

        // 调用核心逻辑
        calculate_points_internal(
            implicit_rpn,
            std::vector<std::string>{}, // direct
            explicit_rpn,
            explicit_parametric,        // <--- 传入普通参数方程
            industry_rpn,
            industry_parametric,
            offset_x, offset_y, zoom, screen_width, screen_height
        );

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "--- Native EXE: 计算完成 ---\n";
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "总点数: " << wasm_final_contiguous_buffer.size() << std::endl;

        // 验证 Ranges
        for(size_t i=0; i<wasm_function_ranges_buffer.size(); ++i) {
            auto& r = wasm_function_ranges_buffer[i];
            std::cout << "Func " << i << ": Start=" << r.start_index << ", Count=" << r.point_count << std::endl;
        }

        // 导出点数据到文件
        std::ofstream outfile("points.txt");
        if (outfile.is_open()) {
            outfile << std::fixed << std::setprecision(6);
            for (const auto& p : wasm_final_contiguous_buffer) {
                outfile << p.position.x << " " << p.position.y << " " << p.function_index << "\n";
            }
            outfile.close();
            std::cout << "已将点数据写入 points.txt" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

#endif // __EMSCRIPTEN__