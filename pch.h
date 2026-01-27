#ifndef PCH_H
#define PCH_H
#define NOMINMAX
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
#include <unordered_set>
// 第三方库
#include <xsimd/xsimd.hpp>
#include "oneapi/tbb/concurrent_vector.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/combinable.h"
#include <oneapi/tbb/parallel_for_each.h>
#include <oneapi/tbb/concurrent_queue.h>
// --- 全局共享的核心数据结构与宏定义 ---
#include <bitset>
namespace xs = xsimd;

template <typename T>
using AlignedVector = std::vector<T, xsimd::aligned_allocator<T>>;

struct Vec2 { double x; double y; };
struct Vec2f { float x; float y; };
struct Vec2i { int16_t x, y; };
struct PointData { int16_t x, y; };
struct FunctionResult { Vec2i start, end; };

#include <expected>
#include <string_view>



using batch_type = xs::batch<double>;

// 定义一个可以在编译期获取 SIMD 宽度的常量。
constexpr size_t BATCH_SIZE = batch_type::size;
// 全局模拟 Buffer (对应 WASM 里的 SharedArrayBuffer)
#ifndef NULL_ID
#define NULL_ID 0xFFFFFFFF
#endif




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