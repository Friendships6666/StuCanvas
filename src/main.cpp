#include <iostream>
#include <vector>
#include <chrono>
#include <cstdint>
#include <iomanip>

// 定义压缩后的点结构


// 防止编译器优化掉结果
void do_not_optimize(void* p) {
    static volatile void* sink;
    sink = p;
}

int main() {
    const size_t N = 1000000; // 1000万个点
    std::vector<PointData> buffer(N);

    // 测试数据范围：从 (-30000, -30000) 到 (30000, 30000)
    const int16_t start_val = -30000;
    const int16_t end_val = 30000;

    std::cout << "--- Linear Interpolation Performance Test (10M Points) ---" << std::endl;

    // ==========================================
    // 方案 A: 32位 浮点数插值 (Float)
    // ==========================================
    {
        auto s = std::chrono::high_resolution_clock::now();

        float fx = (float)start_val;
        float fy = (float)start_val;
        float dx = (float)(end_val - start_val) / (float)(N - 1);
        float dy = (float)(end_val - start_val) / (float)(N - 1);

        PointData* ptr = buffer.data();
        for (size_t i = 0; i < N; ++i) {
            // 注意：这里包含了浮点到整数的截断开销
            ptr[i].x = (int16_t)fx;
            ptr[i].y = (int16_t)fy;
            fx += dx;
            fy += dy;
        }

        auto e = std::chrono::high_resolution_clock::now();
        do_not_optimize(buffer.data());
        std::chrono::duration<double, std::milli> ms = e - s;
        std::cout << std::left << std::setw(25) << "Float32 Interpolation:"
                  << ms.count() << " ms" << std::endl;
    }

    // ==========================================
    // 方案 B: 16.16 定点数插值 (Int32 Fixed-Point)
    // ==========================================
    {
        auto s = std::chrono::high_resolution_clock::now();

        // 将 int16 左移 16 位进入高位，低 16 位作为小数
        int32_t ix = (int32_t)start_val << 16;
        int32_t iy = (int32_t)start_val << 16;

        // 计算步长（由于结果可能很小，先左移再除法保留精度）
        int32_t idx = ((int32_t)(end_val - start_val) << 16) / (int32_t)(N - 1);
        int32_t idy = ((int32_t)(end_val - start_val) << 16) / (int32_t)(N - 1);

        PointData* ptr = buffer.data();
        for (size_t i = 0; i < N; ++i) {
            // 仅需右移，无需复杂的类型转换函数
            ptr[i].x = (int16_t)(ix >> 16);
            ptr[i].y = (int16_t)(iy >> 16);
            ix += idx;
            iy += idy;
        }

        auto e = std::chrono::high_resolution_clock::now();
        do_not_optimize(buffer.data());
        std::chrono::duration<double, std::milli> ms = e - s;
        std::cout << std::left << std::setw(25) << "16.16 Fixed-Point:"
                  << ms.count() << " ms" << std::endl;
    }

    // ==========================================
    // 验证：输出最后一个点，确保逻辑正确
    // ==========================================
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Verification (Last Point): x=" << buffer[N-1].x << " y=" << buffer[N-1].y << std::endl;

    return 0;
}