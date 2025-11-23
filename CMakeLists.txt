# 设置所需的最低 CMake 版本 (为 target_precompile_headers 提高到 3.16)
cmake_minimum_required(VERSION 3.16)

# 定义项目名称和语言
project(WasmTBBTest CXX)

# 设置 C++ 标准为 C++23 (保持您原有的设置)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 定义可执行文件目标。
# 重要的是，要将 pch.h 和 pch.cpp 也加入源文件列表，
# 这样 CMake 和 CLion 才能正确地跟踪它们。
add_executable(WasmTBBTest src/main.cpp
        pch.h
        pch.cpp
        include/CAS/RPN/RPN.h
        src/CAS/RPN/RPN.cpp
        include/Functions/functions.h

        include/Functions/lerp.h

        include/plot/plotImplicit.h
        src/plot/plotImplicit.cpp
        include/plot/plotExplicit.h
        src/plot/plotExplicit.cpp
        include/plot/plotParametric.h
        src/plot/plotParametric.cpp
        include/plot/plotCall.h
        src/plot/plotCall.cpp
        include/interval/interval.h
        src/interval/IntervalEvaluate.cpp
        include/CAS/symbolic/GraphicSimplify.h
        include/CAS/symbolic/GraphicSimplify.h
        src/CAS/symbolic/GraphicSimplify.cpp
        include/CAS/AST/JsonAdapter.h
        include/CAS/AST/JsonAdapter.h
        src/CAS/AST/JsonAdapter.cpp
        include/plot/plotIndustry.h
        src/plot/plotIndustry.cpp
        include/interval/MultiInterval.h
        include/CAS/RPN/MultiIntervalRPN.h


)

# --------------------------------------------------------------------
# --- 关键部分：启用预编译头文件 ---
# --------------------------------------------------------------------
# target_precompile_headers 是 CMake 3.16+ 的现代标准命令。
# 它告诉 CMake，WasmTBBTest 这个目标将使用 pch.h 作为预编译头文件。
# PRIVATE 意味着这个设置只应用于 WasmTBBTest 自身。
target_precompile_headers(WasmTBBTest PRIVATE pch.h)
find_package(Boost REQUIRED COMPONENTS multiprecision)
find_package(Boost REQUIRED COMPONENTS pool)
target_link_libraries(WasmTBBTest PRIVATE Boost::pool)
# 查找 PkgConfig 模块，这是调用 pkgconf 的核心
find_package(PkgConfig REQUIRED)
find_package(simdjson REQUIRED)
# 使用 PkgConfig 查找 GMP 和 MPFR，并创建导入目标
# (这里的语法完全复制自 vcpkg 的输出)
pkg_check_modules(gmp REQUIRED IMPORTED_TARGET gmp)
pkg_check_modules(mpfr REQUIRED IMPORTED_TARGET mpfr)


#  gmp
find_package(PkgConfig REQUIRED)
pkg_check_modules(gmp REQUIRED IMPORTED_TARGET gmp)


# gmpxx
find_package(PkgConfig REQUIRED)
pkg_check_modules(gmpxx REQUIRED IMPORTED_TARGET gmpxx)

find_package(PkgConfig)
pkg_check_modules(mpfr REQUIRED IMPORTED_TARGET mpfr)




target_link_libraries(WasmTBBTest PRIVATE
        simdjson::simdjson
        Boost::multiprecision

        # --- NEW: 链接 GMP::gmp 和 MPFR::mpfr ---
        PkgConfig::gmp
        PkgConfig::gmpxx

        PkgConfig::mpfr
)
if(TARGET xsimd::xsimd)
    message(STATUS "Found modern xsimd target: xsimd::xsimd")
    target_link_libraries(WasmTBBTest PRIVATE xsimd::xsimd)
else()
    message(STATUS "Using legacy xsimd include directory: ${xsimd_INCLUDE_DIRS}")
    target_include_directories(WasmTBBTest PRIVATE ${xsimd_INCLUDE_DIRS})
endif()


# --- 平台专属配置 (保持您原有的逻辑) ---
if(EMSCRIPTEN)
    message(STATUS "Emscripten toolchain detected. Applying WASM-specific flags.")

    # 启用 TBB, Pthreads, SIMD, 和内存增长
    target_link_options(WasmTBBTest PRIVATE
            -s USE_TBB=1
            -s PTHREADS=1
            -s ALLOW_MEMORY_GROWTH=1
    )
    target_compile_options(WasmTBBTest PRIVATE -msimd128)
else()
    # --- 原生编译配置 ---
    message(STATUS "Native toolchain detected. Finding local TBB package.")
    find_package(TBB REQUIRED)
    target_link_libraries(WasmTBBTest PRIVATE TBB::tbb)

    # 为不同的编译器添加特定的 SIMD 架构控制选项
    if(MSVC)
        # 对于 MSVC 编译器, 使用 /arch:AVX2 来启用 256 位 SIMD
        message(STATUS "MSVC compiler detected. Using /arch:AVX2 to target 256-bit SIMD.")
        target_compile_options(WasmTBBTest PRIVATE "/arch:AVX2")
        add_compile_options(/utf-8)
    else()
        # 对于 GCC/Clang, 使用 -mavx2.
        message(STATUS "GCC/Clang compatible compiler detected. Using -mavx2.")
        target_compile_options(WasmTBBTest PRIVATE -mavx2)
        add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()

endif()