#ifndef PCH_H
#define PCH_H
#define NOMINMAX
#include "simdjson.h"       // 引入 simdjson
#include <nlohmann/json.hpp> // 保留: 用于序列化
// 标准库
#include <iostream>
#include <vector>

#include <string>
#include <sstream>
#include <cmath>
#include <stdexcept>
#include <optional>
#include <array>
#include <limits>
#include <utility>
#include <thread>
#include <chrono>
#include <stack>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cstdint>

// 第三方库
#include <xsimd/xsimd.hpp>
#include "oneapi/tbb/concurrent_vector.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/combinable.h"
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/concurrent_queue.h>
// --- 全局共享的核心数据结构与宏定义 ---

namespace xs = xsimd;

template <typename T>
using AlignedVector = std::vector<T, xsimd::aligned_allocator<T>>;

struct Vec2 { double x; double y; };
struct Vec2f { float x; float y; };
struct PointData { Vec2f position; unsigned int function_index; };
static_assert(sizeof(PointData) == 12, "PointData size/padding mismatch! Expected 24 bytes.");

struct FunctionRange {
    uint32_t start_index;
    uint32_t point_count;
};
static_assert(sizeof(FunctionRange) == 8, "FunctionRange size mismatch! Expected 8 bytes.");

struct Uniforms { Vec2 screen_dimensions; double zoom; Vec2 offset; };
using batch_type = xs::batch<double>;

// 定义一个可以在编译期获取 SIMD 宽度的常量。
constexpr size_t BATCH_SIZE = batch_type::size;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline))
#endif

using batch_type = xs::batch<double>;

#endif //PCH_H