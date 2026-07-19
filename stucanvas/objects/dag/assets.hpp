#pragma once
#include <bitset>
#include <cstdint>
#include <string>

#include "compact_string.hpp"
#include "data.hpp"
#include "flex_vector.hpp"
#include "function.hpp"
#include "interval.hpp"
#include "node_types.hpp"
#include "tiny_vector.hpp"

namespace StuCanvas::DAGAssets
{
    // =========================================================================
    // 1. 空间与定义域限制（Spatial & Domain Bounds）
    // =========================================================================

    // 1D 区间定义域
    struct xDomain
    {
        double min_val;
        double max_val;
    };

    struct yDomain
    {
        double min_val;
        double max_val;
    };


    struct zDomain
    {
        double min_val;
        double max_val;
    };


    struct tDomain
    {
        double min_val;
        double max_val;
    };
    struct uDomain
    {
        double min_val;
        double max_val;
    };

    struct vDomain
    {
        double min_val;
        double max_val;
    };


    // 离散化世界步长
    struct DiscretizationStep
    {
        double value;
    };

    // =========================================================================
    // 2. 一元显式标量函数（Explicit Unary Scalar Functions）
    // =========================================================================

    // 一元显函数：y = f(x)
    struct ExplicitScalarFnYFromX
    {
        utils::StuFunction< double ( double ) > fn;
    };

    // 一元显函数：x = f(y)
    struct ExplicitScalarFnXFromY
    {
        utils::StuFunction< double ( double ) > fn;
    };

    // =========================================================================
    // 3. 二元显式标量函数排列组合（Explicit Binary Scalar Functions）
    // =========================================================================

    // 二元显函数：z = f(x, y)
    struct ExplicitScalarFnZFromXY
    {
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 二元显函数：y = f(x, z)
    struct ExplicitScalarFnYFromXZ
    {
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 二元显函数：x = f(y, z)
    struct ExplicitScalarFnXFromYZ
    {
        utils::StuFunction< double ( double, double ) > fn;
    };

    // =========================================================================
    // 4. 隐式代数函数（Implicit Functions）
    // =========================================================================

    // 2D 隐式曲线：f(x, y) = 0
    struct ImplicitFn2D
    {
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 3D 隐式曲面：f(x, y, z) = 0
    struct ImplicitFn3D
    {
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // =========================================================================
    // 5. 参数化函数（Parametric Functions）
    // =========================================================================

    // 2D 参数化曲线：x = f(t), y = g(t)
    struct ParametricCurve2D
    {
        utils::StuFunction< double ( double ) > x_fn;
        utils::StuFunction< double ( double ) > y_fn;
    };

    struct ParametricCurve3D
    {
        utils::StuFunction< double ( double ) > x_fn;
        utils::StuFunction< double ( double ) > y_fn;
        utils::StuFunction< double ( double ) > z_fn;
    };


    struct ParametricSurface3D
    {
        utils::StuFunction< double ( double, double ) > x_fn;
        utils::StuFunction< double ( double, double ) > y_fn;
        utils::StuFunction< double ( double, double ) > z_fn;
    };

    // 对 y = f(x) 求导：d^n y / dx^n
    struct ExplicitDerivativeFnYWrtX
    {
        uint32_t order;   // 导数阶数（1为一阶导数，2为二阶...）
        utils::StuFunction< double ( double ) > fn;
    };

    // 对 x = f(y) 求导：d^n x / dy^n
    struct ExplicitDerivativeFnXWrtY
    {
        uint32_t order;
        utils::StuFunction< double ( double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 7.2 二元显函数偏导数 (Binary Explicit Partial Derivatives)
    // ─────────────────────────────────────────────────────────────────────────

    // 针对 z = f(x, y) ── 对 x 求偏导：d^n z / dx^n
    struct ExplicitPartialDerivativeFnZWrtX
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 针对 z = f(x, y) ── 对 y 求偏导：d^n z / dy^n
    struct ExplicitPartialDerivativeFnZWrtY
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 针对 y = f(x, z) ── 对 x 求偏导：d^n y / dx^n
    struct ExplicitPartialDerivativeFnYWrtX
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 针对 y = f(x, z) ── 对 z 求偏导：d^n y / dz^n
    struct ExplicitPartialDerivativeFnYWrtZ
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 针对 x = f(y, z) ── 对 y 求偏导：d^n x / dy^n
    struct ExplicitPartialDerivativeFnXWrtY
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 针对 x = f(y, z) ── 对 z 求偏导：d^n x / dz^n
    struct ExplicitPartialDerivativeFnXWrtZ
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 7.3 二元隐函数导数 (2D Implicit Curve Derivatives - f(x, y) = 0)
    // ─────────────────────────────────────────────────────────────────────────

    // 隐函数导数：dy / dx（输入当前点坐标 x, y 进行求值）
    struct ImplicitDerivativeFnYWrtX2D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 隐函数导数：dx / dy（输入当前点坐标 x, y 进行求值）
    struct ImplicitDerivativeFnXWrtY2D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 7.4 三元隐函数偏导数 (3D Implicit Surface Partial Derivatives - f(x, y, z) = 0)
    // ─────────────────────────────────────────────────────────────────────────

    // 隐函数偏导：dz / dx（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnZWrtX3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 隐函数偏导：dz / dy（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnZWrtY3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 隐函数偏导：dy / dx（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnYWrtX3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 隐函数偏导：dy / dz（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnYWrtZ3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 隐函数偏导：dx / dy（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnXWrtY3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 隐函数偏导：dx / dz（输入空间点坐标 x, y, z 进行求值）
    struct ImplicitPartialDerivativeFnXWrtZ3D
    {
        uint32_t order;
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // =========================================================================
    // 8. 积分、偏积分与多重积分特型资产（Calculus: Integrals, Partial & Multiple Integrals）
    // =========================================================================

    // ─────────────────────────────────────────────────────────────────────────
    // 8.1 一元显函数定积分 (Unary Explicit Integrals)
    // ─────────────────────────────────────────────────────────────────────────

    // 对 y = f(x) 求定积分：∫[lower_x, upper_x] f(x) dx
    struct ExplicitIntegralFnYFromX
    {
        // 接受积分下限（lower_x）和积分上限（upper_x），返回积分结果
        utils::StuFunction< double ( double, double ) > fn;
    };

    // 对 x = f(y) 求定积分：∫[lower_y, upper_y] f(y) dy
    struct ExplicitIntegralFnXFromY
    {
        utils::StuFunction< double ( double, double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 8.2 二元显函数偏积分 (Binary Explicit Partial Integrals)
    // ─────────────────────────────────────────────────────────────────────────

    // 针对 z = f(x, y) ── 对 x 偏积分：I(y) = ∫[lower_x, upper_x] f(x, y) dx
    struct ExplicitPartialIntegralFnZWrtX
    {
        // 接受常量 y 的值、x 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 针对 z = f(x, y) ── 对 y 偏积分：I(x) = ∫[lower_y, upper_y] f(x, y) dy
    struct ExplicitPartialIntegralFnZWrtY
    {
        // 接受常量 x 的值、y 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 针对 y = f(x, z) ── 对 x 偏积分：I(z) = ∫[lower_x, upper_x] f(x, z) dx
    struct ExplicitPartialIntegralFnYWrtX
    {
        // 接受常量 z 的值、x 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 针对 y = f(x, z) ── 对 z 偏积分：I(x) = ∫[lower_z, upper_z] f(x, z) dz
    struct ExplicitPartialIntegralFnYWrtZ
    {
        // 接受常量 x 的值、z 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 针对 x = f(y, z) ── 对 y 偏积分：I(z) = ∫[lower_y, upper_y] f(y, z) dy
    struct ExplicitPartialIntegralFnXWrtY
    {
        // 接受常量 z 的值、y 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // 针对 x = f(y, z) ── 对 z 偏积分：I(y) = ∫[lower_z, upper_z] f(y, z) dz
    struct ExplicitPartialIntegralFnXWrtZ
    {
        // 接受常量 y 的值、z 的积分下限和上限
        utils::StuFunction< double ( double, double, double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 8.3 二重积分 (Double Integrals)
    // ─────────────────────────────────────────────────────────────────────────

    // 针对 z = f(x, y) ── 对 dx dy 区域二重积分：∬ f(x, y) dy dx
    struct DoubleIntegralFnZFromXY
    {
        // 依次接受：x 的积分下/上限、y 的积分下/上限
        utils::StuFunction< double ( double, double, double, double ) > fn;
    };

    // 针对 y = f(x, z) ── 对 dx dz 区域二重积分：∬ f(x, z) dz dx
    struct DoubleIntegralFnYFromXZ
    {
        // 依次接受：x 的积分下/上限、z 的积分下/上限
        utils::StuFunction< double ( double, double, double, double ) > fn;
    };

    // 针对 x = f(y, z) ── 对 dy dz 区域二重积分：∬ f(y, z) dz dy
    struct DoubleIntegralFnXFromYZ
    {
        // 依次接受：y 的积分下/上限、z 的积分下/上限
        utils::StuFunction< double ( double, double, double, double ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 8.4 三重积分 (Triple Integrals)
    // ─────────────────────────────────────────────────────────────────────────

    // 针对 3D 标量场 ── 对 dx dy dz 体积三重积分：∭ f(x, y, z) dz dy dx
    struct TripleIntegralFn3D
    {
        // 依次接受：x 的下/上限、y 的下/上限、z 的下/上限
        utils::StuFunction< double ( double, double, double, double, double, double ) > fn;
    };

    // =========================================================================
    // 9. 区间算术特型资产（Calculus: Interval Arithmetic Functions）
    // =========================================================================

    // ─────────────────────────────────────────────────────────────────────────
    // 9.1 一元显式区间函数 (Unary Explicit Interval Functions)
    // ─────────────────────────────────────────────────────────────────────────

    // 一元区间函数：Y = F(X)
    struct ExplicitIntervalFnYFromX
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn;
    };

    // 一元区间函数：X = F(Y)
    struct ExplicitIntervalFnXFromY
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 9.2 二元显式区间函数 (Binary Explicit Interval Functions)
    // ─────────────────────────────────────────────────────────────────────────

    // 二元区间函数：Z = F(X, Y)
    struct ExplicitIntervalFnZFromXY
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // 二元区间函数：Y = F(X, Z)
    struct ExplicitIntervalFnYFromXZ
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // 二元区间函数：X = F(Y, Z)
    struct ExplicitIntervalFnXFromYZ
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 9.3 三元显式区间函数 (Ternary Explicit Interval Functions)
    // ─────────────────────────────────────────────────────────────────────────

    // 三元区间函数：W = F(X, Y, Z)
    struct ExplicitIntervalFnWFromXYZ
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // 三元区间函数：Z = F(X, Y, W)
    struct ExplicitIntervalFnZFromXYW
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // 三元区间函数：Y = F(X, Z, W)
    struct ExplicitIntervalFnYFromXZW
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // 三元区间函数：X = F(Y, Z, W)
    struct ExplicitIntervalFnXFromYZW
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            fn;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // 9.4 参数区间曲线与曲面 (Parametric Interval Curves & Surfaces)
    // ─────────────────────────────────────────────────────────────────────────

    // 2D 参数区间曲线：X = F(T), Y = G(T)
    struct ParametricIntervalCurve2D
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn;
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn;
    };

    // 3D 参数区间曲线：X = F(T), Y = G(T), Z = H(T)
    struct ParametricIntervalCurve3D
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn;
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn;
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > z_fn;
    };

    // 3D 参数区间曲面：X = F(U, V), Y = G(U, V), Z = H(U, V)
    struct ParametricIntervalSurface3D
    {
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            x_fn;
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            y_fn;
        utils::StuFunction< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                           const utils::IntervalSet< double >& ) >
            z_fn;
    };


    struct PointCloud2D_SoA
    {
        utils::TinyVector< double > x;
        utils::TinyVector< double > y;
    };


    struct PointCloud3D_SoA
    {
        utils::TinyVector< double > x;
        utils::TinyVector< double > y;
        utils::TinyVector< double > z;
    };
    // =========================================================================
    // 12. 极致性能：折线与拓扑三角网格 SoA 资产 (Line Strips & Meshes SoA)
    // =========================================================================

    // 2D 连续折线带 SoA ── 坐标解耦平铺：X 连续，Y 连续
    struct LineStrip2D_SoA
    {
        utils::TinyVector< double > x;
        utils::TinyVector< double > y;
    };

    // 3D 连续空间折线带 SoA ── 坐标解耦平铺：X 连续，Y 连续，Z 连续
    struct LineStrip3D_SoA
    {
        utils::TinyVector< double > x;
        utils::TinyVector< double > y;
        utils::TinyVector< double > z;
    };

    // 3D 拓扑三角网格面 SoA ── 承接 Marching Cubes 算法输出的顶点与面片拓扑
    struct TriangleMesh3D_SoA
    {
        // 顶点坐标解耦平铺：X 连续，Y 连续，Z 连续。完美匹配 AVX 向量求交与多 VBO 绑定
        utils::TinyVector< double > x;
        utils::TinyVector< double > y;
        utils::TinyVector< double > z;

        // 三角面片索引扁平化排布：[i0, j0, k0, i1, j1, k1...]。完美匹配显卡 Index Buffer
        utils::TinyVector< uint32_t > indices;
    };

    // 并行 CPU 核心数量配置资产
    struct CpuCoreCount
    {
        uint32_t value;
    };

    // L-SHADE（线性成功历史自适应差分进化）算法配置资产
    struct LShade
    {
        uint32_t initial_population_size;   // 初始探路者数量（初始种群大小，如 100）
        uint32_t min_population_size;       // 保底探路者数量（最小种群大小，如 4）
        uint32_t max_evaluations;           // 最大尝试次数（最大进化代数/评估次数，如 15000）
        uint32_t seed;                      // 随机数种子（用于高频重现性随机数生成）
    };


}   // namespace StuCanvas::DAGAssets
