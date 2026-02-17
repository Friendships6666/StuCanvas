cmake_minimum_required(VERSION 3.16)
project(GeoEngine LANGUAGES CXX C)

# =========================================================
# 1. 基础编译配置
# =========================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")

# 压制 CMake 策略警告 (针对 FindBoost)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# =========================================================
# 2. 源文件列表
# =========================================================
set(SOURCE_FILES
        pch.h
        pch.cpp
        src/main.cpp
        include/graph/GeoGraph.h
        # 请根据需要取消注释下面的核心文件
        # src/CAS/RPN/SyntaxChecker.cpp
        # src/CAS/RPN/FormulaNormalizer.cpp
        # src/graph/GeoGraph.cpp
        # ...
)

# =========================================================
# 3. 外部依赖配置 (Arch Linux 修复版)
# =========================================================
# 1. 查找 Dawn 库
# 安装后，Dawn 会提供 CMake 配置文件
find_package(Dawn REQUIRED)

# 2. 查找 GLFW（通常你也需要窗口系统）
# 确保你已经 pacman -S glfw-x11 或 glfw-wayland
find_package(glfw3 REQUIRED)
# 3.1 PkgConfig (用于查找 GMP/MPFR)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GMP REQUIRED gmp)
pkg_check_modules(MPFR REQUIRED mpfr)

# 3.2 TBB & xsimd
find_package(TBB REQUIRED)
find_package(xsimd REQUIRED)
# 3.5 CGAL (Header-only，但需要链接依赖)
find_package(CGAL REQUIRED)

# 3.6 Lua (标准包查找)
find_package(Lua REQUIRED)

# 3.7 REBOUND (手动查找 - 假设安装在 /usr/local)
find_path(REBOUND_INCLUDE_DIR rebound.h PATHS /usr/local/include /usr/include)
find_library(REBOUND_LIBRARY NAMES rebound PATHS /usr/local/lib /usr/lib)
if(NOT REBOUND_INCLUDE_DIR OR NOT REBOUND_LIBRARY)
    message(FATAL_ERROR "Could NOT find REBOUND library/headers. Did you run 'sudo make install'?")
endif()
# 3.3 simdjson
find_package(simdjson REQUIRED)

# 3.4 GSL
find_package(GSL REQUIRED)

# 3.5 SymEngine (使用自定义安装路径)
set(SymEngine_DIR "/usr/local/lib/cmake/symengine")
find_package(SymEngine REQUIRED)

# 3.6 Eigen3
find_package(Eigen3 REQUIRED)

# 3.7 Boost (关键修复)
# ---------------------------------------------------------
# 1. 强制使用 Module 模式，避开 Arch 上 Config 模式的路径问题
set(Boost_NO_BOOST_CMAKE ON)
# 2. 不请求特定组件(如 system/unit_test)，仅查找头文件核心
#    Odeint 和 Multiprecision 都是 Header-only 的
find_package(Boost REQUIRED)
# ---------------------------------------------------------

# =========================================================
# 4. 构建目标
# =========================================================
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 预编译头文件
target_precompile_headers(${PROJECT_NAME} PRIVATE pch.h)

# =========================================================
# 5. 头文件路径与宏
# =========================================================
target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include

        ${EIGEN3_INCLUDE_DIR}
        ${Boost_INCLUDE_DIRS}
        ${GMP_INCLUDE_DIRS}
        ${MPFR_INCLUDE_DIRS}
        /usr/local/include  # 确保能找到手动编译的库头文件
        ${REBOUND_INCLUDE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        HAVE_LIBMPFR
        SIZEOF_VOID_P=8
        SMARTPTR64
        NDEBUG
)

target_compile_options(${PROJECT_NAME} PRIVATE
        -O3 -march=native -flto
        -fomit-frame-pointer
        -funroll-loops
        -fno-semantic-interposition
)
target_link_options(${PROJECT_NAME} PRIVATE -O3 -march=native -flto)

# =========================================================
# 6. 链接库
# =========================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
        dawn::webgpu_dawn
    dawn_native
       dawn_proc
        glfw
        # 并行与向量化
        TBB::tbb
        xsimd
        Eigen3::Eigen

        # 基础数学库 (通过 PkgConfig 变量链接)
        ${MPFR_LIBRARIES}
        ${GMP_LIBRARIES}

        # JSON
        simdjson::simdjson

        # 科学计算
        GSL::gsl
        GSL::gslcblas
        symengine

        # Boost (只链接头文件接口，不需要 system/thread 库文件)
        Boost::headers
        CGAL::CGAL           # CGAL 核心库 (Header-only 也会有链接时需要的目标)
        ${REBOUND_LIBRARY}   # Rebound 库
        lua             # Lua 库
)