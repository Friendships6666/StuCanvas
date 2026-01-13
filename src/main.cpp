#include "../pch.h"
#include "../include/graph/GeoEngine.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <gen.h>
#include <giac.h>
// 辅助导出函数：将当前显存中的点数据导出为文本
void ExportPoints(const std::string& filename) {
    std::ofstream outfile(filename);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return;
    }

    // 写入元数据头
    outfile << "# Export: " << filename << "\n";
    outfile << "# Total Buffer Size: " << wasm_final_contiguous_buffer.size() << "\n";
    outfile << "# [X_Clip] [Y_Clip] [Func_ID]\n";
    outfile << std::fixed << std::setprecision(6);

    // 遍历全局 Buffer
    for (const auto& pt : wasm_final_contiguous_buffer) {
        outfile << pt.position.x << " "
                << pt.position.y << " "
                << pt.function_index << "\n";
    }
    outfile.close();
    std::cout << "-> [Disk] Saved to " << filename << " (" << wasm_final_contiguous_buffer.size() << " points)" << std::endl;
}
void test_hell_level_cas() {
    giac::context ctx;
    auto run_test = [&](const std::string& label, const std::string& command) {
        std::cout << "\n[HELL TEST] " << label << ": " << command << std::endl;
        auto start = std::chrono::high_resolution_clock::now();

        giac::gen result(command, &ctx);
        result = giac::eval(result, 1, &ctx);
        // 对于极其复杂的代数式，ratnormal 比 simplify 在处理分式和多项式时更底层且高效
        result = giac::ratnormal(result, &ctx);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Result: " << result.print(&ctx) << std::endl;
        std::cout << "Time  : " << duration.count() << " us" << std::endl;
    };

    // --- 1. Gröbner 基计算 (Gröbner Basis) ---
    // 这是代数几何的基石。解决多项式理想问题。
    // 复杂度是指数级的。如果这个能过，说明 Giac 的多项式内核达到了专业级。
    run_test("Gröbner Basis", "gbasis([x^2 + y^2 - 1, x^2 - z^2 + y, x + y + z], [x, y, z])");

    // --- 2. 三重嵌套符号积分 (Triple Nested Integral) ---
    // 连续对三个变量求不定积分，测试 AST 的递归深度和变量阴影处理
    run_test("Triple Integral", "integrate(integrate(integrate(x*y*z, x), y), z)");

    // --- 3. 线性微分方程组 (Systems of ODEs) ---
    // 解两个耦合的微分方程。非常考验矩阵指数（Matrix Exponential）算法。
    run_test("ODE System", "desolve([diff(x(t),t) = y(t), diff(y(t),t) = -x(t)], [x(t), y(t)])");

    // --- 4. 符号矩阵求逆 (4x4 Symbolic Matrix Inverse) ---
    // 4x4 矩阵，每个元素都是符号变量。这会产生极其庞大的中间行列式展开。
    run_test("4x4 Sym Matrix Inv", "inv([[a,b,0,0],[c,d,0,0],[0,0,e,f],[0,0,g,h]])");

    // --- 5. 贝塞尔函数导数 (Bessel Function Identities) ---
    // 特殊函数的导数推导。测试特殊函数库及其递归关系。
    // 预期结果应包含 bessel_j(n-1, x) 或类似项
    run_test("Bessel Diff", "diff(bessel_j(n, x), x)");

    // --- 6. 符号极限 (二元未定式 / 路径相关性尝试) ---
    // 这是一个经典的测试。


    // --- 7. 离散求和恒等式 (Discrete Summation) ---
    // 计算组合数的求和。
    run_test("Combinatoric Sum", "sum(comb(n, k), k, 0, n)");
}
int main() {
    try {
        std::cout << "=== GeoEngine Full Cycle Test ===" << std::endl;
        giac::vecteur v;
        v.push_back(600);   // window_h
        v.push_back(800);   // window_w
        v.push_back(100);  // legende_size
        v.push_back(30);   // coord_size
        v.push_back(20);   // param_step
        v.push_back(0);    // 0: radians, 1: degrees

        // 2. 调用你发现的那个 bool 版 cas_setup
        // contextptr 传 nullptr 表示初始化全局默认设置
        giac::cas_setup(v, nullptr);
        giac::context ctx;
        giac::gen warmup = giac::eval(giac::gen("1+1", &ctx), 1, &ctx);

        // 2. 强制定义一个变量并执行一次简单的 normal 运算
        // 这会强制 Giac 初始化全局常量表（如 zero, plus_one 等）



        test_hell_level_cas();




        // 0. 初始化引擎 (2560x1600)
        GeoEngine engine(2560.0, 1600.0);

        // =========================================================
        // Step 1 & 2: 创建与初始化渲染 -> points1.txt
        // =========================================================
        std::cout << "\n[Phase 1] Create Objects (A, B, Line)" << std::endl;

        // 提交创建事务 (此时仅在内存中建立逻辑，尚未渲染)
        uint32_t idA = engine.AddPoint(-5.0, 0.0);
        uint32_t idB = engine.AddPoint(5.0, 0.0);
        uint32_t idLine = engine.AddLine(idA, idB);

        // 执行第一次渲染 (Incremental 模式：填充空 Buffer)
        engine.Render();

        std::cout << "Buffer Status: Initialized." << std::endl;
        ExportPoints("points1.txt");

        // =========================================================
        // Step 3: 拖拽更新 (局部增量) -> points2.txt
        // =========================================================
        std::cout << "\n[Phase 2] Move Point A to (-10.0, 5.0)" << std::endl;

        // 提交移动事务
        engine.MovePoint(idA, -10.0, 5.0);

        // 执行渲染 (Incremental 模式：追加新数据到 Buffer 末尾)
        engine.Render();

        std::cout << "Buffer Status: Appended new positions." << std::endl;
        // 验证 Ring Buffer：Line 的 Offset 应该指向了 Buffer 后部
        auto& rangeLine = wasm_function_ranges_buffer[2]; // draw_order[2] 是线
        std::cout << "Debug: Line Offset moved to " << rangeLine.start_index << std::endl;

        ExportPoints("points2.txt");

        // =========================================================
        // Step 4: 撤销操作 (Undo) -> points3.txt
        // =========================================================
        std::cout << "\n[Phase 3] Undo Move (Ctrl+Z)" << std::endl;

        // 执行撤销 (逻辑回滚 + 增量渲染旧位置)
        engine.Undo();
        engine.Render();

        std::cout << "Buffer Status: Appended restored positions (A back to -5.0)." << std::endl;
        ExportPoints("points3.txt");

        // =========================================================
        // Step 4.5: 重做操作 (Redo) -> points4.txt
        // =========================================================
        std::cout << "\n[Phase 4] Redo Move (Ctrl+Shift+Z)" << std::endl;

        // 执行重做 (逻辑重演 + 增量渲染新位置)
        engine.Redo();
        engine.Render();

        std::cout << "Buffer Status: Appended redo positions (A back to -10.0)." << std::endl;
        ExportPoints("points4.txt");

        // =========================================================
        // Step 5: 视图缩放 (全量重刷) -> points5.txt
        // =========================================================
        std::cout << "\n[Phase 5] Viewport Zoom (x2.0)" << std::endl;

        // 提交视图变更事务
        engine.PanZoom(0.0, 0.0, 0.2); // Zoom 0.1 -> 0.2

        // 执行渲染 (Viewport 模式：检测到视图变化，清空 Buffer 并重投影)
        engine.Render();

        std::cout << "Buffer Status: Cleared & Reprojected." << std::endl;
        ExportPoints("points5.txt");

        std::cout << "\n=== Test Complete ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}