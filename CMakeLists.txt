cmake_minimum_required(VERSION 3.16)
project(GeoEngine LANGUAGES CXX C)

# =========================================================
# 1. 基础编译配置
# =========================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 修复：预先定义 CGAL 需要的变量，防止冲突
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_VERSION ON)

# =========================================================
# 2. 查找外部依赖 (注意顺序！)
# =========================================================

# A. 先找 CGAL (它会自动处理并查找 GMP 和 MPFR)
# 不要在这里写 pkg_check_modules(GMP/MPFR)
find_package(CGAL REQUIRED)

# B. 基础库
find_package(glfw3 REQUIRED)
find_package(TBB REQUIRED)
find_package(xsimd REQUIRED)
find_package(Lua REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)

# C. SymEngine & Boost
set(SymEngine_DIR "/usr/local/lib/cmake/symengine")
find_package(SymEngine REQUIRED)
find_package(Boost REQUIRED)






set(SOURCE_FILES
        pch.h
        pch.cpp
        src/main.cpp
        include/gpu/utils.hpp
        include/gpu/math_utils.hpp
        include/gpu/geometry.hpp
        include/gpu/context.hpp
        include/gpu/camera.hpp
        include/graph/GeoGraph.h
        include/plot/plotExplicit3D.hpp
        include/gpu/app.hpp

)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_precompile_headers(${PROJECT_NAME} PRIVATE pch.h)

# =========================================================
# 5. 配置头文件与宏
# =========================================================
target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include



        ${EIGEN3_INCLUDE_DIR}
        /usr/local/include

)

# 使用 CGAL 找到的 GMP/MPFR 路径
target_include_directories(${PROJECT_NAME} PRIVATE ${CGAL_INCLUDE_DIRS})

target_compile_definitions(${PROJECT_NAME} PRIVATE
        HAVE_LIBMPFR
        SIZEOF_VOID_P=8
        SMARTPTR64
        NDEBUG
        O3
)

# =========================================================
# 6. 链接库
# =========================================================
target_link_libraries(${PROJECT_NAME} PRIVATE

        # Dawn 库

        dawn::webgpu_dawn


        SDL3

        # 基础依赖
        glfw
        TBB::tbb
        xsimd
        Eigen3::Eigen
        symengine
        Boost::headers
        CGAL::CGAL



        lua
        dl
        pthread
)