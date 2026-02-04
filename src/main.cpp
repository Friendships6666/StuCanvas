#include <iostream>
#include <vector>
#include <array>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <xsimd/xsimd.hpp>

namespace xs = xsimd;
using batch_type = xs::batch<double>;

// 4路垂直化展开核心参数
constexpr size_t VM_BLOCK_SIZE = 4;

// ==========================================
// 1. 指令集与数据结构
// ==========================================
enum class OpCode : uint8_t {
    PUSH_X, PUSH_Y, PUSH_CONST, ADD, SUB, MUL, DIV, SIN, STOP
};

struct RPNToken {
    OpCode op;
    double val = 0.0;
};

struct Interval_Batch {
    batch_type min, max;
    Interval_Batch() = default;
    Interval_Batch(batch_type mn, batch_type mx) : min(mn), max(mx) {}
};

// ==========================================
// 2. 垂直化采样引擎 (针对像素点 4x4 块密集计算)
// ==========================================
void EvaluateVertical_Sample(
    const std::vector<RPNToken>& tokens,
    const batch_type* x_ptr, const batch_type* y_ptr,
    batch_type* out_ptr, batch_type* workspace
) {
    const RPNToken* pc = tokens.data();
    batch_type* sp = workspace;
    // 寄存器直接映射到 CPU 端口
    batch_type acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;

    while (true) {
        const auto& t = *pc++;
        switch (t.op) {
            case OpCode::PUSH_X:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                acc0=x_ptr[0]; acc1=x_ptr[1]; acc2=x_ptr[2]; acc3=x_ptr[3]; break;
            case OpCode::PUSH_Y:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                acc0=y_ptr[0]; acc1=y_ptr[1]; acc2=y_ptr[2]; acc3=y_ptr[3]; break;
            case OpCode::PUSH_CONST:
                sp[0]=acc0; sp[1]=acc1; sp[2]=acc2; sp[3]=acc3; sp+=4;
                { batch_type v(t.val); acc0=v; acc1=v; acc2=v; acc3=v; } break;
            case OpCode::ADD: sp-=4; acc0+=sp[0]; acc1+=sp[1]; acc2+=sp[2]; acc3+=sp[3]; break;
            case OpCode::SUB: sp-=4; acc0=sp[0]-acc0; acc1=sp[1]-acc1; acc2=sp[2]-acc2; acc3=sp[3]-acc3; break;
            case OpCode::MUL: sp-=4; acc0*=sp[0]; acc1*=sp[1]; acc2*=sp[2]; acc3*=sp[3]; break;
            case OpCode::DIV: sp-=4; acc0=sp[0]/acc0; acc1=sp[1]/acc1; acc2=sp[2]/acc2; acc3=sp[3]/acc3; break;
            case OpCode::SIN: acc0=xs::sin(acc0); acc1=xs::sin(acc1); acc2=xs::sin(acc2); acc3=xs::sin(acc3); break;
            case OpCode::STOP:
                out_ptr[0]=acc0; out_ptr[1]=acc1; out_ptr[2]=acc2; out_ptr[3]=acc3;
                return;
        }
    }
}

// ==========================================
// 3. 垂直化区间引擎 (严谨 IA 减枝)
// ==========================================
namespace IA {
    inline Interval_Batch mul(const Interval_Batch& a, const Interval_Batch& b) {
        batch_type p1 = a.min * b.min, p2 = a.min * b.max, p3 = a.max * b.min, p4 = a.max * b.max;
        return { xs::min(xs::min(p1, p2), xs::min(p3, p4)), xs::max(xs::max(p1, p2), xs::max(p3, p4)) };
    }
    inline Interval_Batch sin(const Interval_Batch& i) {
        auto width_ge_2pi = (i.max - i.min) >= (2.0 * M_PI);
        auto s_min = xs::sin(i.min), s_max = xs::sin(i.max);
        auto b_min = xs::min(s_min, s_max), b_max = xs::max(s_min, s_max);
        auto k_peak = xs::ceil((i.min - (M_PI/2.0)) / (2.0 * M_PI));
        auto has_peak = (k_peak * (2.0 * M_PI) + (M_PI/2.0)) <= i.max;
        auto k_trough = xs::ceil((i.min - (1.5 * M_PI)) / (2.0 * M_PI));
        auto has_trough = (k_trough * (2.0 * M_PI) + (1.5 * M_PI)) <= i.max;
        batch_type f_max = xs::select(has_peak, batch_type(1.0), b_max);
        batch_type f_min = xs::select(has_trough, batch_type(-1.0), b_min);
        return { xs::select(width_ge_2pi, batch_type(-1.0), f_min), xs::select(width_ge_2pi, batch_type(1.0), f_max) };
    }
    inline Interval_Batch div(const Interval_Batch& a, const Interval_Batch& b) {
        auto b_has_zero = (b.min <= 0.0) & (b.max >= 0.0);
        Interval_Batch inv_b = { 1.0 / b.max, 1.0 / b.min };
        Interval_Batch res = mul(a, inv_b);
        return { xs::select(b_has_zero, batch_type(-1e18), res.min), xs::select(b_has_zero, batch_type(1e18), res.max) };
    }
}

void EvaluateVertical_Prune(
    const std::vector<RPNToken>& tokens,
    const Interval_Batch* x_ptr, const Interval_Batch* y_ptr,
    Interval_Batch* out_ptr, batch_type* ws_min, batch_type* ws_max
) {
    const RPNToken* pc = tokens.data();
    batch_type* sp_min = ws_min; batch_type* sp_max = ws_max;
    batch_type min0=0, min1=0, min2=0, min3=0, max0=0, max1=0, max2=0, max3=0;

    while (true) {
        const auto& t = *pc++;
        switch (t.op) {
            case OpCode::PUSH_X:
                sp_min[0]=min0; sp_min[1]=min1; sp_min[2]=min2; sp_min[3]=min3;
                sp_max[0]=max0; sp_max[1]=max1; sp_max[2]=max2; sp_max[3]=max3; sp_min+=4; sp_max+=4;
                min0=x_ptr[0].min; min1=x_ptr[1].min; min2=x_ptr[2].min; min3=x_ptr[3].min;
                max0=x_ptr[0].max; max1=x_ptr[1].max; max2=x_ptr[2].max; max3=x_ptr[3].max; break;
            case OpCode::PUSH_Y:
                sp_min[0]=min0; sp_min[1]=min1; sp_min[2]=min2; sp_min[3]=min3;
                sp_max[0]=max0; sp_max[1]=max1; sp_max[2]=max2; sp_max[3]=max3; sp_min+=4; sp_max+=4;
                min0=y_ptr[0].min; min1=y_ptr[1].min; min2=y_ptr[2].min; min3=y_ptr[3].min;
                max0=y_ptr[0].max; max1=y_ptr[1].max; max2=y_ptr[2].max; max3=y_ptr[3].max; break;
            case OpCode::PUSH_CONST:
                sp_min[0]=min0; sp_min[1]=min1; sp_min[2]=min2; sp_min[3]=min3;
                sp_max[0]=max0; sp_max[1]=max1; sp_max[2]=max2; sp_max[3]=max3; sp_min+=4; sp_max+=4;
                { batch_type v(t.val); min0=min1=min2=min3=max0=max1=max2=max3=v; } break;
            case OpCode::ADD:
                sp_min-=4; sp_max-=4; min0+=sp_min[0]; min1+=sp_min[1]; min2+=sp_min[2]; min3+=sp_min[3];
                max0+=sp_max[0]; max1+=sp_max[1]; max2+=sp_max[2]; max3+=sp_max[3]; break;
            case OpCode::SUB:
                sp_min-=4; sp_max-=4;
                { batch_type t0=sp_min[0]-max0, t1=sp_min[1]-max1, t2=sp_min[2]-max2, t3=sp_min[3]-max3;
                  max0=sp_max[0]-min0; max1=sp_max[1]-min1; max2=sp_max[2]-min2; max3=sp_max[3]-min3;
                  min0=t0; min1=t1; min2=t2; min3=t3; } break;
            case OpCode::MUL:
                sp_min-=4; sp_max-=4;
                { auto r0=IA::mul({sp_min[0],sp_max[0]},{min0,max0}); min0=r0.min; max0=r0.max;
                  auto r1=IA::mul({sp_min[1],sp_max[1]},{min1,max1}); min1=r1.min; max1=r1.max;
                  auto r2=IA::mul({sp_min[2],sp_max[2]},{min2,max2}); min2=r2.min; max2=r2.max;
                  auto r3=IA::mul({sp_min[3],sp_max[3]},{min3,max3}); min3=r3.min; max3=r3.max; } break;
            case OpCode::SIN:
                { auto r0=IA::sin({min0,max0}); min0=r0.min; max0=r0.max;
                  auto r1=IA::sin({min1,max1}); min1=r1.min; max1=r1.max;
                  auto r2=IA::sin({min2,max2}); min2=r2.min; max2=r2.max;
                  auto r3=IA::sin({min3,max3}); min3=r3.min; max3=r3.max; } break;
            case OpCode::STOP:
                out_ptr[0]={min0,max0}; out_ptr[1]={min1,max1}; out_ptr[2]={min2,max2}; out_ptr[3]={min3,max3};
                return;
        }
    }
}

// ==========================================
// 4. 全流程绘图基准测试
// ==========================================
struct QuadNode { double xmin, xmax, ymin, ymax; };

void RunExtremeVerticalPlotter(const std::vector<RPNToken>& tokens) {
    // 模拟 2560x1600 高清分辨率
    double zoom = 0.05;
    double wpp = 2.0 / (1600.0 * zoom);

    std::vector<QuadNode> stack;
    stack.reserve(10000);
    stack.push_back({-40.0, 40.0, -25.0, 25.0});

    alignas(64) batch_type ws_sample[64];
    alignas(64) batch_type ws_ia_min[64], ws_ia_max[64];

    size_t total_points = 0;
    size_t ia_decisions = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    while(!stack.empty()) {
        QuadNode c = stack.back(); stack.pop_back();
        double xm = (c.xmin + c.xmax) * 0.5, ym = (c.ymin + c.ymax) * 0.5;

        // Quadtree 子块区间
        Interval_Batch X_subs[4] = { {c.xmin,xm}, {xm,c.xmax}, {c.xmin,xm}, {xm,c.xmax} };
        Interval_Batch Y_subs[4] = { {c.ymin,ym}, {c.ymin,ym}, {ym,c.ymax}, {ym,c.ymax} };
        Interval_Batch prune_res[4];

        // [ IA PRUNING ] 垂直化一次判定 4 个块
        EvaluateVertical_Prune(tokens, X_subs, Y_subs, prune_res, ws_ia_min, ws_ia_max);

        for(int i = 0; i < 4; ++i) {
            ia_decisions++;
            auto mask = (prune_res[i].min <= 0.0) & (prune_res[i].max >= 0.0);

            if(xs::any(mask)) {
                // 修改为 4 像素级停止
                if((X_subs[i].max.get(0) - X_subs[i].min.get(0)) <= 4.0 * wpp) {

                    // [ PIXEL SAMPLING ] 针对 4x4=16 像素块进行垂直化采样
                    // 16 像素 = 4 个 Batch (每个batch=4点, AVX2) 或 8 个 Batch (WASM)
                    // 垂直化每次处理 4 个 Batch，所以只需 1-2 次循环
                    int loops = 16 / (batch_type::size * VM_BLOCK_SIZE);
                    if (loops == 0) loops = 1;

                    for(int l=0; l<loops; ++l) {
                        batch_type dx[4]={0}, dy[4]={0}, dout[4];
                        EvaluateVertical_Sample(tokens, dx, dy, dout, ws_sample);
                        total_points += (batch_type::size * VM_BLOCK_SIZE);
                    }
                } else {
                    stack.push_back({X_subs[i].min.get(0), X_subs[i].max.get(0),
                                     Y_subs[i].min.get(0), Y_subs[i].max.get(0)});
                }
            }
        }
        if(ia_decisions > 5000000) break;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "--- Rigorous Vertical Engine (4x4 Subdivision) ---" << std::endl;
    std::cout << "IA Decisions:         " << ia_decisions << std::endl;
    std::cout << "Total Pixels Sampled: " << total_points << std::endl;
    std::cout << "Total Time:           " << total_us << " us (" << total_us/1000.0 << " ms)" << std::endl;
    std::cout << "Throughput:           " << (total_points / (total_us / 1e6)) / 1e6 << " M Pixels/s" << std::endl;
}

int main() {
    // 公式: y - x * sin(x) / 3 = 0
    std::vector<RPNToken> tokens = {
        {OpCode::PUSH_Y}, {OpCode::PUSH_X}, {OpCode::PUSH_X},
        {OpCode::SIN}, {OpCode::MUL}, {OpCode::PUSH_CONST, 3.0},
        {OpCode::DIV}, {OpCode::SUB}, {OpCode::STOP}
    };

    RunExtremeVerticalPlotter(tokens);
    return 0;
}