#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <xsimd/xsimd.hpp>

// 严格依赖项目提供的头文件
#include "include/graph/GeoGraph.h"
#include "include/interval/interval.h"
#include "include/CAS/RPN/ShuntingYard.h"

namespace xs = xsimd;
using batch_type = xs::batch<double>;

// ====================================================================
// 1. 数据结构定义 (针对单核极致优化)
// ====================================================================

struct ImplicitTask {
    double x, y, size;
};

// 预分配大池：消除 malloc 抖动
constexpr size_t MAX_POOL_SIZE = 1024 * 256;
thread_local static ImplicitTask Pool_IA_A[MAX_POOL_SIZE];
thread_local static ImplicitTask Pool_IA_B[MAX_POOL_SIZE];
thread_local static ImplicitTask Pool_Sampling[MAX_POOL_SIZE];

// ====================================================================
// 2. 极致性能 RPN 引擎 - 采样版 (4路展开)
// ====================================================================
void EvaluateRPN_Fast_Point(
    const std::vector<RPNToken>& tokens,
    const batch_type* __restrict x_ptr,
    const batch_type* __restrict y_ptr,
    batch_type* __restrict out_ptr,
    size_t num_batches,
    void* __restrict workspace
) {
    auto* __restrict v_stack_base = static_cast<batch_type*>(workspace);
    // 核心修正：由于外部传来的 NUM_BATCHES 是 8，这里正好可以跑两次内层循环
    for (size_t b = 0; b < num_batches; b += 4) {
        const RPNToken* pc = tokens.data();
        batch_type* __restrict sp = v_stack_base;
        batch_type acc0, acc1, acc2, acc3;

        while (true) {
            const auto& token = *pc++;
            switch (token.type) {
                case RPNTokenType::PUSH_X:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    acc0 = x_ptr[b]; acc1 = x_ptr[b+1]; acc2 = x_ptr[b+2]; acc3 = x_ptr[b+3];
                    break;
                case RPNTokenType::PUSH_Y:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    acc0 = y_ptr[b]; acc1 = y_ptr[b+1]; acc2 = y_ptr[b+2]; acc3 = y_ptr[b+3];
                    break;
                case RPNTokenType::PUSH_CONST:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    { batch_type vc(token.value); acc0 = vc; acc1 = vc; acc2 = vc; acc3 = vc; }
                    break;
                case RPNTokenType::ADD: sp -= 4; acc0 += sp[0]; acc1 += sp[1]; acc2 += sp[2]; acc3 += sp[3]; break;
                case RPNTokenType::SUB: sp -= 4; acc0 = sp[0] - acc0; acc1 = sp[1] - acc1; acc2 = sp[2] - acc2; acc3 = sp[3] - acc3; break;
                case RPNTokenType::MUL: sp -= 4; acc0 *= sp[0]; acc1 *= sp[1]; acc2 *= sp[2]; acc3 *= sp[3]; break;
                case RPNTokenType::DIV: sp -= 4; acc0 = sp[0] / acc0; acc1 = sp[1] / acc1; acc2 = sp[2] / acc2; acc3 = sp[3] / acc3; break;
                case RPNTokenType::SIN: acc0 = xs::sin(acc0); acc1 = xs::sin(acc1); acc2 = xs::sin(acc2); acc3 = xs::sin(acc3); break;
                case RPNTokenType::COS: acc0 = xs::cos(acc0); acc1 = xs::cos(acc1); acc2 = xs::cos(acc2); acc3 = xs::cos(acc3); break;
                case RPNTokenType::SQRT: acc0 = xs::sqrt(acc0); acc1 = xs::sqrt(acc1); acc2 = xs::sqrt(acc2); acc3 = xs::sqrt(acc3); break;
                case RPNTokenType::STOP:
                    out_ptr[b] = acc0; out_ptr[b+1] = acc1; out_ptr[b+2] = acc2; out_ptr[b+3] = acc3;
                    goto block_done;
                default: break;
            }
        }
        block_done:;
    }
}

// ====================================================================
// 3. 极致性能 RPN 引擎 - 区间版 (用于剪枝)
// ====================================================================
void EvaluateRPN_Fast_IA(
    const std::vector<RPNToken>& tokens,
    const Interval_Batch* __restrict x_ptr,
    const Interval_Batch* __restrict y_ptr,
    Interval_Batch* __restrict out_ptr,
    size_t num_batches,
    void* __restrict workspace
) {
    auto* __restrict v_stack_base = static_cast<Interval_Batch*>(workspace);
    for (size_t b = 0; b < num_batches; b += 4) {
        const RPNToken* pc = tokens.data();
        Interval_Batch* __restrict sp = v_stack_base;
        Interval_Batch acc0, acc1, acc2, acc3;

        while (true) {
            const auto& token = *pc++;
            switch (token.type) {
                case RPNTokenType::PUSH_X:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    acc0 = x_ptr[b]; acc1 = x_ptr[b+1]; acc2 = x_ptr[b+2]; acc3 = x_ptr[b+3];
                    break;
                case RPNTokenType::PUSH_Y:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    acc0 = y_ptr[b]; acc1 = y_ptr[b+1]; acc2 = y_ptr[b+2]; acc3 = y_ptr[b+3];
                    break;
                case RPNTokenType::PUSH_CONST:
                    sp[0] = acc0; sp[1] = acc1; sp[2] = acc2; sp[3] = acc3; sp += 4;
                    { batch_type vc(token.value); acc0 = {vc, vc}; acc1 = {vc, vc}; acc2 = {vc, vc}; acc3 = {vc, vc}; }
                    break;
                case RPNTokenType::ADD: sp -= 4; acc0 = interval_add_batch(sp[0], acc0); acc1 = interval_add_batch(sp[1], acc1); acc2 = interval_add_batch(sp[2], acc2); acc3 = interval_add_batch(sp[3], acc3); break;
                case RPNTokenType::SUB: sp -= 4; acc0 = interval_sub_batch(sp[0], acc0); acc1 = interval_sub_batch(sp[1], acc1); acc2 = interval_sub_batch(sp[2], acc2); acc3 = interval_sub_batch(sp[3], acc3); break;
                case RPNTokenType::MUL: sp -= 4; acc0 = interval_mul_batch(sp[0], acc0); acc1 = interval_mul_batch(sp[1], acc1); acc2 = interval_mul_batch(sp[2], acc2); acc3 = interval_mul_batch(sp[3], acc3); break;
                case RPNTokenType::DIV: sp -= 4; acc0 = interval_div_batch(sp[0], acc0); acc1 = interval_div_batch(sp[1], acc1); acc2 = interval_div_batch(sp[2], acc2); acc3 = interval_div_batch(sp[3], acc3); break;
                case RPNTokenType::SIN: acc0 = interval_sin_batch(acc0); acc1 = interval_sin_batch(acc1); acc2 = interval_sin_batch(acc2); acc3 = interval_sin_batch(acc3); break;
                case RPNTokenType::COS: acc0 = interval_cos_batch(acc0); acc1 = interval_cos_batch(acc1); acc2 = interval_cos_batch(acc2); acc3 = interval_cos_batch(acc3); break;
                case RPNTokenType::STOP:
                    out_ptr[b] = acc0; out_ptr[b+1] = acc1; out_ptr[b+2] = acc2; out_ptr[b+3] = acc3;
                    goto block_done;
                default: break;
            }
        }
        block_done:;
    }
}

// ====================================================================
// 4. 函数二：4x4 采样 Kernel (Fused Sample Kernel)
// ====================================================================
void Fused_Sample_Kernel(
    GeometryGraph& graph,
    const std::vector<RPNToken>& tokens,
    ImplicitTask* tasks,
    size_t task_count
) {
    const auto& v = graph.view;
    auto& final_buffer = graph.final_points_buffer;

    // 核心修正：25个格点，为了满足 Evaluate 引擎的 4 路展开，我们将 Batch 数设为 8 (32个槽位)
    constexpr size_t NUM_BATCHES = 8;
    alignas(64) batch_type gx[NUM_BATCHES], gy[NUM_BATCHES], gv[NUM_BATCHES];
    alignas(64) batch_type vm_workspace[256];

    for (size_t t = 0; t < task_count; ++t) {
        const ImplicitTask& task = tasks[t];
        const double step = task.size / 4.0;
        double col_off[5], row_off[5];
        for(int i=0; i<5; ++i) { col_off[i] = task.x + i*step; row_off[i] = task.y - i*step; }

        for (size_t b = 0; b < NUM_BATCHES; ++b) {
            alignas(64) double tx[batch_type::size] = {0}, ty[batch_type::size] = {0};
            for (int k = 0; k < 4; ++k) {
                size_t idx = b * 4 + k;
                if (idx < 25) { tx[k] = col_off[idx % 5]; ty[k] = row_off[idx / 5]; }
                else { tx[k] = 1e30; ty[k] = 1e30; }
            }
            gx[b] = batch_type::load_aligned(tx); gy[b] = batch_type::load_aligned(ty);
        }

        EvaluateRPN_Fast_Point(tokens, gx, gy, gv, NUM_BATCHES, vm_workspace);

        alignas(64) double val_grid[32];
        for(size_t b=0; b<NUM_BATCHES; ++b) gv[b].store_aligned(&val_grid[b*4]);

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                int i0 = r*5+c, i1 = i0+1, i2 = i0+5, i3 = i2+1;
                double v0 = val_grid[i0], v1 = val_grid[i1], v2 = val_grid[i2], v3 = val_grid[i3];

                auto check_lerp = [&](double va, double vb, double xa, double ya, double xb, double yb) {
                    if ((va * vb) <= 0.0 && va != vb) {
                        double tr = va / (va - vb);
                        int16_t cx = static_cast<int16_t>((xa + tr*(xb-xa) - v.offset_x) * v.ndc_scale_x);
                        int16_t cy = static_cast<int16_t>((ya + tr*(yb-ya) - v.offset_y) * v.ndc_scale_y);
                        final_buffer.push_back({cx, cy});
                    }
                };
                check_lerp(v0, v1, col_off[c], row_off[r], col_off[c+1], row_off[r]);
                check_lerp(v0, v2, col_off[c], row_off[r], col_off[c], row_off[r+1]);
            }
        }
    }
}

// ====================================================================
// 5. 函数一：IA 任务泵 (IA Subdivision Pump)
// ====================================================================
void IA_Subdivision_Pump(GeometryGraph& graph, const std::vector<RPNToken>& tokens) {
    const auto& v = graph.view;
    const double threshold = 3.5 * v.wpp; // 四叉树细分到 3*3 像素附近，向上取整

    ImplicitTask* src = Pool_IA_A;
    ImplicitTask* dst = Pool_IA_B;
    size_t src_count = 1;
    size_t sampling_count = 0;

    // 初始任务：全屏。注意：初始 size 必须涵盖整个视口
    double initial_size = std::max(v.screen_width, v.screen_height) * v.wpp;
    src[0] = { v.offset_x - (v.screen_width * 0.5 * v.wpp), v.offset_y + (v.screen_height * 0.5 * v.wpp), initial_size };

    alignas(64) Interval_Batch ib_x[4], ib_y[4], ib_res[4];
    alignas(64) Interval_Batch ia_workspace[256];

    while (src_count > 0) {
        size_t dst_count = 0;
        for (size_t i = 0; i < src_count; ++i) {
            ImplicitTask p = src[i];
            double sub = p.size * 0.5;
            double x_m = p.x + sub, y_m = p.y - sub;

            alignas(64) double tx_min[batch_type::size] = {0}, tx_max[batch_type::size] = {0};
            alignas(64) double ty_min[batch_type::size] = {0}, ty_max[batch_type::size] = {0};

            tx_min[0]=p.x; tx_min[1]=x_m; tx_min[2]=p.x; tx_min[3]=x_m;
            tx_max[0]=x_m; tx_max[1]=p.x+p.size; tx_max[2]=x_m; tx_max[3]=p.x+p.size;
            ty_min[0]=y_m; ty_min[1]=y_m; ty_min[2]=p.y-p.size; ty_min[3]=p.y-p.size;
            ty_max[0]=p.y; ty_max[1]=p.y; ty_max[2]=y_m; ty_max[3]=y_m;

            ib_x[0] = { batch_type::load_aligned(tx_min), batch_type::load_aligned(tx_max) };
            ib_y[0] = { batch_type::load_aligned(ty_min), batch_type::load_aligned(ty_max) };

            EvaluateRPN_Fast_IA(tokens, ib_x, ib_y, ib_res, 4, ia_workspace);

            auto alive = (ib_res[0].min <= 0.0) & (ib_res[0].max >= 0.0);
            uint32_t mask = 0;
            if (alive.get(0)) mask |= 1; if (alive.get(1)) mask |= 2;
            if (alive.get(2)) mask |= 4; if (alive.get(3)) mask |= 8;

            for (int k = 0; k < 4; ++k) {
                if (mask & (1 << k)) {
                    ImplicitTask c = { (k%2==0)?p.x:x_m, (k<2)?p.y:y_m, sub };
                    if (c.size <= threshold) Pool_Sampling[sampling_count++] = c;
                    else dst[dst_count++] = c;
                }
            }
        }
        std::swap(src, dst); src_count = dst_count;
    }
    Fused_Sample_Kernel(graph, tokens, Pool_Sampling, sampling_count);
}

// ====================================================================
// 6. 顶层接口与测试
// ====================================================================
void calculate_implicit_core(GeometryGraph& graph, const std::vector<RPNToken>& tokens) {
    graph.final_points_buffer.clear();
    IA_Subdivision_Pump(graph, tokens);
}
void ExportPointsToFile(const std::vector<PointData>& buffer) {
    if (buffer.empty()) {
        std::cout << "Warning: Buffer is empty, nothing to export.\n";
        return;
    }

    std::ofstream ofs("points.txt");
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not open points.txt for writing.\n";
        return;
    }

    // 使用 \n 而非 std::endl 以避免频繁刷新缓冲区导致的 IO 性能下降
    for (const auto& p : buffer) {
        ofs << p.x << " " << p.y << "\n";
    }

    ofs.close();
    std::cout << "Successfully exported " << buffer.size() << " points to points.txt\n";
}


std::vector<RPNToken> ParseRPNText(const std::string& input) {
    std::vector<RPNToken> tokens;
    std::stringstream ss(input);
    std::string item;

    while (ss >> item) {
        // 1. 处理坐标变量
        if (item == "x") {
            tokens.push_back({RPNTokenType::PUSH_X, 0.0});
        }
        else if (item == "y") {
            tokens.push_back({RPNTokenType::PUSH_Y, 0.0});
        }
        // 2. 处理基础算术运算
        else if (item == "+") {
            tokens.push_back({RPNTokenType::ADD, 0.0});
        }
        else if (item == "-") {
            tokens.push_back({RPNTokenType::SUB, 0.0});
        }
        else if (item == "*") {
            tokens.push_back({RPNTokenType::MUL, 0.0});
        }
        else if (item == "/") {
            tokens.push_back({RPNTokenType::DIV, 0.0});
        }
        // 3. 处理内置数学函数
        else if (item == "sin") {
            tokens.push_back({RPNTokenType::SIN, 0.0});
        }
        else if (item == "cos") {
            tokens.push_back({RPNTokenType::COS, 0.0});
        }
        else if (item == "sqrt") {
            tokens.push_back({RPNTokenType::SQRT, 0.0});
        }
        // 4. 处理常数 (如果不是以上符号，则尝试解析为 double)
        else {
            try {
                double val = std::stod(item);
                tokens.push_back({RPNTokenType::PUSH_CONST, val});
            } catch (...) {
                std::cerr << "Unknown RPN Token: " << item << std::endl;
            }
        }
    }

    // 5. 极致引擎强制要求的终止符
    tokens.push_back({RPNTokenType::STOP, 0.0});

    return tokens;
}
int main() {
    // 1. 初始化图解引擎上下文
    GeometryGraph graph;
    graph.view.offset_x = 0;
    graph.view.offset_y = 0;
    graph.view.zoom = 0.05; // 适当缩放以看清细分效果
    graph.view.screen_width = 2560;
    graph.view.screen_height = 1600;
    graph.view.Refresh(); // 刷新预计算系数 (ndc_scale 等)

    std::string userInput;


    std::cout << "Enter RPN expression (e.g., 'x x * y y * + 3 -'): ";
    std::getline(std::cin, userInput);

    // 解析用户输入的文本
    std::vector<RPNToken> tokens = ParseRPNText(userInput);

    // 2. 手动构建极致性能 RPN 指令序列
    // 逻辑：x -> x -> * -> y -> y -> * -> + -> 3 -> - -> STOP


    // 3. 执行解算并计时
    auto t1 = std::chrono::high_resolution_clock::now();

    // 直接调用核心，不产生任何解析开销
    calculate_implicit_core(graph, tokens);


    auto t2 = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    ExportPointsToFile(graph.final_points_buffer);

    // 4. 输出结果
    std::cout << "==========================================\n";
    std::cout << "Implicit Rendering (Direct Tokens Mode)\n";
    std::cout << "==========================================\n";

    std::cout << "Output Points: " << graph.final_points_buffer.size() << "\n";
    std::cout << "Processing Time: " << duration << " us (" << duration / 1000.0 << " ms)\n";
    std::cout << "==========================================\n";

    return 0;
}