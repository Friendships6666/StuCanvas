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
# 2. 系统库依赖检索 (从系统全局路径寻找)
# ============================================================================
find_package(Eigen3 REQUIRED CONFIG) # API 稳定，直接加载系统配置
find_package(TBB REQUIRED)           # 工业级并行库，直接从系统加载
find_package(Vulkan REQUIRED)        # 显卡底层标准，直接从系统加载

# ============================================================================
# 3. 本地高变动依赖库物理定位
# ============================================================================
set(LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/libs/linux")

set(NVENC_LIB_PATH "/usr/lib/libnvidia-encode.so" CACHE PATH "Path to libnvidia-encode.so")

set(SDL3_EXTERNAL_LIB "${LIBS_DIR}/libSDL3.so")
set(SLANG_EXTERNAL_LIB "${LIBS_DIR}/libslang.so")

# ============================================================================
# 4. Rust Cargo 纯净命令行集成 (已修复重编与进度条宽度参数 Bug)
# ============================================================================
find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(CARGO_BUILD_FLAG "--release")
set(CARGO_TARGET_DIR "release")

set(RUST_LIB_NAME "libtypst_c.so")
set(RUST_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c/target/${CARGO_TARGET_DIR}/${RUST_LIB_NAME}")

# 递归检索并追踪所有 Rust 源码及配置
file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c/src/*.rs"
        "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c/Cargo.toml"
)

add_custom_command(
        OUTPUT "${RUST_LIB_PATH}"
        # 💡 【核心改进】：注入 CARGO_TERM_PROGRESS_WIDTH=80 解决 "always requires a width key" 报错
        COMMAND ${CMAKE_COMMAND} -E env
        CARGO_TERM_COLOR=always
        CARGO_TERM_PROGRESS_WHEN=always
        CARGO_TERM_PROGRESS_WIDTH=80
        ${CARGO_EXECUTABLE} build ${CARGO_BUILD_FLAG} --verbose
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c"
        DEPENDS ${RUST_SOURCES}
        COMMENT "Cargo: Executing verbose build with real-time progress..."
        USES_TERMINAL
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
# 5. 自动分片重组：libslang-llvm.so
# ============================================================================
set(SLANG_LLVM_OUT "${CMAKE_CURRENT_BINARY_DIR}/libslang-llvm.so")
set(SLANG_LLVM_PARTS
        "${LIBS_DIR}/libslang-llvm.so.part.aa"
        "${LIBS_DIR}/libslang-llvm.so.part.ab"
        "${LIBS_DIR}/libslang-llvm.so.part.ac"
)

add_custom_command(
        OUTPUT "${SLANG_LLVM_OUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "Reassembling libslang-llvm.so in compiler cache..."
        COMMAND cat ${SLANG_LLVM_PARTS} > "${SLANG_LLVM_OUT}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS ${SLANG_LLVM_PARTS}
        COMMENT "Reassembling Slang LLVM backend in compiler cache..."
        VERBATIM
)

add_custom_target(reassemble_slang_llvm DEPENDS "${SLANG_LLVM_OUT}")

# 写入临时的物理库同步和资产同步脚本（全目标统配使用）
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sync_libs.cmake" "
file(GLOB libs \"\${SRC_DIR}/*.so*\")
foreach(lib \${libs})
    file(COPY \"\${lib}\" DESTINATION \"\${DST_DIR}\")
endforeach()
file(COPY \"\${SLANG_LLVM_OUT}\" DESTINATION \"\${DST_DIR}\")
")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sync_assets.cmake" "
file(GLOB children RELATIVE \"\${SRC_DIR}\" \"\${SRC_DIR}/*\")
foreach(child \${children})
    if(IS_DIRECTORY \"\${SRC_DIR}/\${child}\")
        if(NOT EXISTS \"\${DST_DIR}/\${child}\")
            file(COPY \"\${SRC_DIR}/\${child}\" DESTINATION \"\${DST_DIR}\")
        endif()
    endif()
endforeach()
")

# ============================================================================
# 6. 核心重构：抽象出共享的核心引擎静态库 StuCanvasCore
# （排除入口文件 typst_test.cpp 和 单元测试 .cpp，保持核心文件只编译一次）
# ============================================================================
add_library(StuCanvasCore STATIC
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
        stucanvas/sobject/sobject.hpp
        stucanvas/sobject/graph.hpp
        stucanvas/sobject/sobject_types.hpp
        stucanvas/rust_bridge/typst_c/typst_c.hpp
        stucanvas/types/cpu/point.hpp
        stucanvas/types/cpu/segment_strips.hpp
        stucanvas/types/cpu/paths.hpp
        stucanvas/types/cpu/triangles.hpp
        stucanvas/types/gpu/point.hpp
        stucanvas/types/cpu/svg_typst.hpp
        stucanvas/types/cpu/cpu_types.hpp
        stucanvas/sobject/sobject_data.hpp
        stucanvas/sobject/solver.hpp
        stucanvas/sobject/discretizer_points.hpp
        stucanvas/sobject/creators.hpp
        stucanvas/sobject/modifiers.hpp
        stucanvas/sobject/getters.hpp
        stucanvas/sobject/instance.hpp
        stucanvas/sobject/family.hpp
        stucanvas/sobject/appearance.hpp
        stucanvas/utils/tiny_vector.hpp
        stucanvas/camera/camera.hpp
        stucanvas/sobject/discretizers.hpp
        # 💡 已从这里彻底移除了 test.cpp
)

# 前置自动分片拼装工作绑定到核心库上
add_dependencies(StuCanvasCore reassemble_slang_llvm)

# 将高级编译选项与系统级标准库绑定为 StuCanvasCore 的 PUBLIC 属性（自动向下游继承）
target_compile_options(StuCanvasCore PUBLIC -O3 -march=native -funroll-loops)
target_compile_definitions(StuCanvasCore PUBLIC BUILD_GTK)

if(ipo_supported)
    set_target_properties(StuCanvasCore PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

target_include_directories(StuCanvasCore PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/rust_bridge/typst_c
        ${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/external
)
target_link_libraries(StuCanvasCore PUBLIC
        Vulkan::Vulkan
        Eigen3::Eigen
        TBB::tbb
        typst_c_lib
        /usr/lib/libLLVM.so
        "${SDL3_EXTERNAL_LIB}"
        "${SLANG_EXTERNAL_LIB}"
        ${NVENC_LIB_PATH}
)

# ============================================================================
# 7. 定义统配目标配置函数 configure_stucanvas_target
# （一键为任何测试注入：RPATH指向、LTO状态以及 POST_BUILD 高保真资产与动态库复制脚本）
# ============================================================================
function(configure_stucanvas_target target_name)
    # 1. 注入 RPATH 指向以实现零配置动态库查找
    set_target_properties(${target_name} PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_WITH_INSTALL_RPATH TRUE
    )
    if(ipo_supported)
        set_target_properties(${target_name} PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
    endif()

    # 2. 注入高精、纯净的 POST_BUILD 同步指令（仅拷贝 .so，丢弃所有 .part.a* 分片，并安全拷贝重组后的 .so）
    add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR="${LIBS_DIR}"
            -DDST_DIR="$<TARGET_FILE_DIR:${target_name}>"
            -DSLANG_LLVM_OUT="${SLANG_LLVM_OUT}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/sync_libs.cmake"

            COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stucanvas/assets"
            -DDST_DIR="$<TARGET_FILE_DIR:${target_name}>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/sync_assets.cmake"

            COMMENT "Post-Build: Syncing runtime libraries and assets for target ${target_name}..."
    )
endfunction()

# ============================================================================
# 8. 编译当前主可执行程序 StuCanvas（运行 typst_test.cpp，自动继承所有编译参数）
# ============================================================================
add_executable(typst_test tests/ffi/typst_test.cpp)
target_link_libraries(typst_test PRIVATE StuCanvasCore)
configure_stucanvas_target(typst_test)


add_executable(tiny_vector_test tests/performance/tiny_vector_test.cpp
        )
target_link_libraries(tiny_vector_test PRIVATE StuCanvasCore)
configure_stucanvas_target(tiny_vector_test)



add_executable(deque_test tests/performance/deque_test.cpp
        

)
target_link_libraries(deque_test PRIVATE StuCanvasCore)
configure_stucanvas_target(deque_test)


#add_executable(ptr_vector_test tests/performance/ptr_vector_test.cpp
#
#)
#target_link_libraries(ptr_vector_test PRIVATE StuCanvasCore)
#configure_stucanvas_target(ptr_vector_test)



#add_executable(ptr_deque_test tests/performance/ptr_deque_test.cpp
#
#)
#target_link_libraries(ptr_deque_test PRIVATE StuCanvasCore)
#configure_stucanvas_target(ptr_deque_test)


add_executable(flat_map_test tests/performance/flat_map_test.cpp
        
)
target_link_libraries(flat_map_test PRIVATE StuCanvasCore)
configure_stucanvas_target(flat_map_test)



add_executable(llvm_tiny_vector_test tests/performance/llvm_tiny_vector_test.cpp

)
target_link_libraries(llvm_tiny_vector_test PRIVATE StuCanvasCore)
configure_stucanvas_target(llvm_tiny_vector_test)



add_executable(deque_vs_vector tests/performance/deque_vs_vector.cpp

)
target_link_libraries(deque_vs_vector PRIVATE StuCanvasCore)
configure_stucanvas_target(deque_vs_vector)