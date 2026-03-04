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
# 1. 强行告诉 CMake 和编译器 CUDA 在哪
set(CUDAToolkit_ROOT "/opt/cuda")
set(CMAKE_CUDA_COMPILER "/opt/cuda/bin/nvcc")
# =========================================================
# 2. 查找外部依赖 (注意顺序！)
# =========================================================

# A. 先找 CGAL (它会自动处理并查找 GMP 和 MPFR)
# 不要在这里写 pkg_check_modules(GMP/MPFR)
find_package(CGAL REQUIRED)
# 1. 查找 Box2D
find_package(box2d REQUIRED)
find_package(absl REQUIRED)
# 2. 查找 FFmpeg (使用 PkgConfig)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavcodec
        libavformat
        libswscale
        libavutil
)

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
find_package(OpenCV REQUIRED COMPONENTS core imgproc objdetect dnn videoio)
find_package(CUDAToolkit REQUIRED COMPONENTS
        cudart
        cufft
        nppc
        nppial
        nppicc
        nppidei
        nppif
        nppig
        nppim
        nppist
        nppisu
        nppitc
)




set(SOURCE_FILES
        pch.h
        pch.cpp

        include/gpu/math_utils.hpp
        include/gpu/geometry.hpp
        include/gpu/context.hpp
        include/gpu/camera.hpp
        include/graph/GeoGraph.h
        include/plot/plotExplicit3D.hpp
        include/gpu/app.hpp
        third_party/imgui/backends/imgui_impl_sdl3.cpp
        third_party/imgui/backends/imgui_impl_wgpu.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui.h
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_internal.h
        third_party/imgui/imconfig.h
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/imstb_textedit.h
        include/gpu/gui.hpp
        third_party/nanosvg/nanosvg.h
        third_party/nanosvg/nanosvgrast.h
        include/gpu/svg_loader.hpp
        src/RingsAndBalls.cpp
        src/video3.cu
        include/video/RingsAndBalls/config.hpp
        include/video/RingsAndBalls/render_utils.hpp
        include/video/RingsAndBalls/ffmpeg_utils.hpp
        include/video/RingsAndBalls/physics_utils.hpp
        webgpu.h
        src/BallOne.cpp
        include/video/BallOne/config.hpp
        include/video/BallOne/physics_utils.hpp
        include/video/BallOne/render_utils.hpp
        include/video/BallOne/ffmpeg_utils.hpp
        src/video3.cu

)

add_executable(${PROJECT_NAME}
        third_party/imgui/backends/imgui_impl_sdl3.cpp
        third_party/imgui/backends/imgui_impl_wgpu.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui.h
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_internal.h
        third_party/imgui/imconfig.h
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/imstb_textedit.h



        src/StuScriptTest.cpp
        include/StuScript/ast.hpp
        include/StuScript/Sema.hpp
        include/StuScript/Diagnostic.hpp
)

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

# 1. 宏定义部分 (仅保留预处理器符号)
target_compile_definitions(${PROJECT_NAME} PRIVATE
        HAVE_LIBMPFR
        SIZEOF_VOID_P=8
        SMARTPTR64
        NDEBUG  # 禁用断言，极大提升速度
)

# 2. 核心优化参数 (Clang 专用极致优化)
target_compile_options(${PROJECT_NAME} PRIVATE
        -O3                    # 基础 3 级优化
        -march=native          # [核心] 针对你 U9 285HX 的架构生成特定指令集 (AVX2, BMI2, etc.)
        -flto             # [核心] 开启薄链路时间优化，跨文件函数内联
        -ffast-math            # [核心] 激进数学优化，放弃严格 IEEE 浮点，对物理引擎和渲染速度提升巨大
        -fomit-frame-pointer   # 释放一个通用寄存器提高效率



)

# 3. 链接器优化
target_link_options(${PROJECT_NAME} PRIVATE
        -flto

)

# 4. 开启 CMake 级别的 IPO (LTO)
set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
# =========================================================
# 6. 链接库
# =========================================================
pkg_check_modules(CUDA_NPPI REQUIRED nppi nppig nppicc)
find_library(NPP_IG_LIB nppig HINTS /opt/cuda/lib64 /usr/lib)
find_library(NPP_CC_LIB nppicc HINTS /opt/cuda/lib64 /usr/lib)
find_library(NPP_C_LIB nppc HINTS /opt/cuda/lib64 /usr/lib)
find_package(LLVM REQUIRED CONFIG)
# 4. 指定 LLVM 需要用到的组件
# 对于设计 DSL 和使用 ORC JIT，以下组件是必须的：

# 输出找到的版本信息（可选，方便调试）
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake from ${LLVM_DIR}")
# 必须添加这一行，否则链接器找不到 -lLLVMCore
link_directories(${LLVM_LIBRARY_DIRS})

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        CUDA::cudart
        CUDA::cufft
        ${NPP_IG_LIB}
        ${NPP_CC_LIB}
        ${NPP_C_LIB}
        ${llvm_libs}

        absl::flat_hash_map
        absl::strings

        # Dawn 库

        dawn::webgpu_dawn
        box2d::box2d
        PkgConfig::FFMPEG
        ${OpenCV_LIBS}
        /usr/lib/libLLVM.so

        SDL3

        # 基础依赖
        glfw
        TBB::tbb
        xsimd
        Eigen3::Eigen
        symengine
        Boost::headers
        CGAL::CGAL
        /usr/lib/libbox2d.so   # <--- 直接在这里写绝对路径
        m



        lua
        dl
        pthread

)
set_target_properties(${PROJECT_NAME} PROPERTIES CUDA_ARCHITECTURES 86)