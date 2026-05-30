cmake_minimum_required(VERSION 3.28) # C++23 稳定支持建议的起始 CMake 版本
project(StuCanvas LANGUAGES CXX)

# ============================================================================
# 1. C++ 编译器与标准配置 (仅针对 Linux 平台优化)
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 严格遵循 C++23 标准

# 检测并启用系统级链接时优化 (LTO / IPO)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
    message(STATUS "IPO/LTO is supported and will be applied to StuCanvas.")
else()
    message(WARNING "IPO/LTO is not supported on this platform: ${error}")
endif()

# ============================================================================
# 2. 系统超稳定库依赖检索 (从系统全局路径寻找)
# ============================================================================
find_package(Eigen3 REQUIRED CONFIG) # API 稳定，直接加载系统配置
find_package(TBB REQUIRED)           # 工业级并行库，直接从系统加载
find_package(Vulkan REQUIRED)        # 显卡底层标准，直接从系统加载
# 【注意】：已彻底删除任何关于 SDL3 或 Slang 的系统级搜寻指令，强制锁死本地版本！

# ============================================================================
# 3. 本地高变动依赖库物理定位 (外置在 stucanvas 文件夹外的根目录下)
# ============================================================================
set(LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/libs/linux") # 物理库路径：external/libs/

# 参数化外置的 Linux 私有驱动库（支持编译期外置传入覆盖）
set(NVENC_LIB_PATH "/usr/lib/libnvidia-encode.so"
        CACHE PATH "Path to libnvidia-encode.so")

# 绑定本地 external 文件夹下的高变动动态链接库，禁止从系统寻找
set(SDL3_EXTERNAL_LIB "${LIBS_DIR}/libSDL3.so")
set(SLANG_EXTERNAL_LIB "${LIBS_DIR}/libslang.so")

# ============================================================================
# 4. Rust Cargo 纯净命令行集成（完全遵循 Cargo.toml 编译配置）
# ============================================================================
find_program(CARGO_EXECUTABLE cargo REQUIRED)


set(CARGO_BUILD_FLAG "--release")
set(CARGO_TARGET_DIR "release")
message(STATUS "[Rust] Configuring Cargo in Release mode.")


set(RUST_LIB_NAME "libtypst_c.so")
set(RUST_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c/target/${CARGO_TARGET_DIR}/${RUST_LIB_NAME}")

# ============================================================================
# 核心联动：加入 USES_TERMINAL 让其直接获取终端控制权，实时输出彩色编译日志
# ============================================================================
add_custom_command(
        OUTPUT "${RUST_LIB_PATH}"
        COMMAND ${CARGO_EXECUTABLE} build ${CARGO_BUILD_FLAG}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c"
        COMMENT "Cargo: Executing build (codegen, LTO, strip are fully controlled by Cargo.toml)..."
        USES_TERMINAL # <-- 【新增】：将当前命令丢入 Ninja 的 console 进程池，强制允许实时、彩色、不带缓冲地输出日志！
        VERBATIM
)

# 建立虚拟目标，并声明依赖该输出文件
add_custom_target(rust_bridge_build DEPENDS "${RUST_LIB_PATH}")

# 导入生成的 FFI 动态库目标
add_library(typst_c_lib SHARED IMPORTED GLOBAL)
set_target_properties(typst_c_lib PROPERTIES
        IMPORTED_LOCATION "${RUST_LIB_PATH}"
)
add_dependencies(typst_c_lib rust_bridge_build)

# ============================================================================
# 5. 自动分片重组：将 Git 里的分片文件还原为 140MB 的 libslang-llvm.so
# 【核心优化】：输出路径设定在编译缓存目录 (${CMAKE_CURRENT_BINARY_DIR}) 下！
# 彻底解决 140MB 巨型文件物理污染源码目录（external/libs/linux）的问题。
# ============================================================================
set(SLANG_LLVM_OUT "${CMAKE_CURRENT_BINARY_DIR}/libslang-llvm.so") # 生成在缓存中
set(SLANG_LLVM_PARTS
        "${LIBS_DIR}/libslang-llvm.so.part.aa"
        "${LIBS_DIR}/libslang-llvm.so.part.ab"
        "${LIBS_DIR}/libslang-llvm.so.part.ac"
)

add_custom_command(
        OUTPUT "${SLANG_LLVM_OUT}"
        # 使用系统 cat 指令在本地编译缓存中无缝拼装
        COMMAND ${CMAKE_COMMAND} -E echo "Reassembling libslang-llvm.so in compiler cache..."
        COMMAND cat ${SLANG_LLVM_PARTS} > "${SLANG_LLVM_OUT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" # 在缓存目录下执行
        DEPENDS ${SLANG_LLVM_PARTS}
        COMMENT "Reassembling Slang LLVM backend in compiler cache..."
        VERBATIM
)

add_custom_target(reassemble_slang_llvm DEPENDS "${SLANG_LLVM_OUT}")

# ============================================================================
# 6. 定义 C++ 构建目标与源文件列表
# ============================================================================
add_executable(StuCanvas main.cpp
        stucanvas/deprecated_geometry/utils/platonic_data.hpp
        stucanvas/deprecated_geometry/graph.hpp
        stucanvas/deprecated_geometry/plotter.hpp
        stucanvas/deprecated_geometry/solver.hpp
        stucanvas/utils/interval.hpp
        stucanvas/plot/implicit_2d.hpp
        stucanvas/plot/implicit_3d.hpp
        stucanvas/plot/parametric_2d.hpp
        stucanvas/plot/parametric_3d.hpp
        stucanvas/utils/block_deque.hpp
        stucanvas/utils/flat_map.hpp
        stucanvas/utils/math_traits.hpp
        stucanvas/utils/deprecated_parallel_for.hpp
        stucanvas/types/point.hpp
        stucanvas/deprecated_reconstruction/statistics.hpp
        stucanvas/types/deprecated_bounding_box.hpp
        stucanvas/deprecated_reconstruction/quick_hull_3d.hpp
        stucanvas/deprecated_reconstruction/convex_decomposition_3d.hpp
        stucanvas/deprecated_reconstruction/export/off.hpp
        stucanvas/types/deprecated_convex_hull_ds.hpp
        stucanvas/cache/hash.hpp
        stucanvas/cache/processor.hpp
        stucanvas/cache/macros.hpp
        stucanvas/deprecated_reconstruction/quick_hull_2d.hpp
        stucanvas/deprecated_reconstruction/voxel_interval_engine_3d.hpp
        stucanvas/deprecated_reconstruction/mesh_welder.hpp
        stucanvas/deprecated_reconstruction/voxel_surface_reconstructor.hpp
        stucanvas/deprecated_reconstruction/qhull_wrapper.hpp
        stucanvas/deprecated_reconstruction/boolean_engine.hpp
        stucanvas/utils/interval_inst.cpp
        stucanvas/deprecated_reconstruction/convex_point_splitter.hpp
        stucanvas/deprecated_reconstruction/simple_voxelizer.hpp
        stucanvas/deprecated_reconstruction/octree_decomposer.hpp
        stucanvas/deprecated_reconstruction/export/obj.hpp
        stucanvas/deprecated_reconstruction/voxel_cluster_decomposer.hpp
        stucanvas/deprecated_reconstruction/field/paramatric_3d.hpp
        stucanvas/types/path.hpp
        stucanvas/deprecated_reconstruction/bezier_fit_2d.hpp
        stucanvas/deprecated_reconstruction/export/svg.hpp
        stucanvas/deprecated_reconstruction/reconstruct.hpp
        stucanvas/types/segment_strip.hpp
        stucanvas/deprecated_reconstruction/bezier_fitting.hpp
        stucanvas/types/triangles.hpp
        stucanvas/canvas/vulkan/swap_chains.hpp
        stucanvas/canvas/vulkan/renderpass.hpp
        stucanvas/canvas/vulkan/render_pipeline.hpp
        stucanvas/canvas/vulkan/buffer.hpp
        stucanvas/canvas/vulkan/present.hpp
        stucanvas/canvas/vulkan/shader_module.hpp
        stucanvas/canvas/canvas.hpp
        stucanvas/animation/animation.hpp
        stucanvas/canvas/vulkan/offscreen_target.hpp
        stucanvas/utils/ivf_writer.hpp
        stucanvas/canvas/vulkan/compute_pipeline.hpp
        stucanvas/canvas/vulkan/nvenc_cuda_encoder.hpp
        external/nvidia/cuda.h
        stucanvas/canvas/vulkan/vk_ctx.hpp
        stucanvas/canvas/vulkan/config.hpp
        stucanvas/utils/av1_writer.hpp
        stucanvas/object/object.hpp
        stucanvas/object/graph.hpp
        stucanvas/object/types.hpp
        stucanvas/rust_bridge/typst_c/typst_c.hpp
)

# 确保在编译主程序前，前置自动分片拼装工作已安全结束
add_dependencies(StuCanvas reassemble_slang_llvm)

# ============================================================================
# 7. 编译器高级编译选项与链接优化
# ============================================================================
target_compile_options(StuCanvas PRIVATE
        -O3
        -march=native
        -funroll-loops
)

if(ipo_supported)
    set_target_properties(StuCanvas PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ============================================================================
# 8. 头文件搜寻路径 (只将外置的 external/ 目录和核心源码目录暴露出来)
# ============================================================================
target_include_directories(StuCanvas PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}                                # 允许 #include "stucanvas/canvas/canvas.hpp"
        ${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c  # 允许直接 #include "typst_c.hpp"

        # 【核心重构】：只把外置的 external 暴露出来。
        # 此时如果 external 目录下有 SDL3 文件夹，代码中可以直接优雅地 `#include <SDL3/SDL.h>`
        ${CMAKE_CURRENT_SOURCE_DIR}/external
)

# ============================================================================
# 9. 库链接关系与运行时 RPATH 指向 (Target Link Libraries & RPATH)
# ============================================================================
target_compile_definitions(StuCanvas PRIVATE BUILD_GTK)

# 让 Linux 编译出来的可执行程序在启动时，优先去自身同级目录下搜寻动态库（零配置运行）
set_target_properties(StuCanvas PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
)

target_link_libraries(StuCanvas PRIVATE
        Vulkan::Vulkan
        Eigen3::Eigen             # 系统级稳定库
        TBB::tbb                  # 系统级并行库
        typst_c_lib               # 本地编译好的 Rust 动态库
        ${TBB_EXTERNAL_LIB}       # 本地外部 TBB 库
        "${SDL3_EXTERNAL_LIB}"    # 强制链接本地绝对路径下的 SDL3，不走系统任何路径
        "${SLANG_EXTERNAL_LIB}"   # 强制链接本地绝对路径下的 Slang 核心编译器，不走系统任何路径
        ${NVENC_LIB_PATH}
)

# ============================================================================
# 10. 运行时自动同步 (POST_BUILD)
# ============================================================================

# A. 【物理库高保真同步脚本】：在编译期运行
# 1. 它通过 *.so* 通配符只拷贝真实的动态库，强行将 140MB 的 .part.a* 分片阻绝在源码区，绝不占用运行目录空间！
# 2. 同时，单独把在编译缓存中组装完毕的 libslang-llvm.so 拷贝到可执行文件所在目录下。
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sync_libs.cmake" "
file(GLOB libs \"\${SRC_DIR}/*.so*\")
foreach(lib \${libs})
    file(COPY \"\${lib}\" DESTINATION \"\${DST_DIR}\")
endforeach()
file(COPY \"\${SLANG_LLVM_OUT}\" DESTINATION \"\${DST_DIR}\")
")

# B. 资产（Assets）文件夹排重同步脚本
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sync_assets.cmake" "
file(GLOB children RELATIVE \"\${SRC_DIR}\" \"\${SRC_DIR}/*\")
foreach(child \${children})
    if(IS_DIRECTORY \"\${SRC_DIR}/\${child}\")
        if(NOT EXISTS \"\${DST_DIR}/\${child}\")
            file(COPY \"\${SRC_DIR}/\${child}\" DESTINATION \"\${DST_DIR}\")
            message(STATUS \"[Assets Sync] Automatically deployed: \${child}\")
        endif()
    endif()
endforeach()
")

# C. 编译后（POST_BUILD）自动触发两套纯净同步
add_custom_command(TARGET StuCanvas POST_BUILD
        # 1. 运行动态库过滤同步（仅拷贝 .so，丢弃所有 .part.a* 分片，并安全拷贝重组后的 .so）
        COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${LIBS_DIR}"
        -DDST_DIR="$<TARGET_FILE_DIR:StuCanvas>"
        -DSLANG_LLVM_OUT="${SLANG_LLVM_OUT}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/sync_libs.cmake"

        # 2. 运行资产排重同步
        COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/assets"
        -DDST_DIR="$<TARGET_FILE_DIR:StuCanvas>"
        -P "${CMAKE_CURRENT_BINARY_DIR}/sync_assets.cmake"

        COMMENT "Post-Build: Running unified runtime libraries and assets synchronization..."
)