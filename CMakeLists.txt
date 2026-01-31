cmake_minimum_required(VERSION 3.16)
project(GeoEngine LANGUAGES CXX C)

# ---------------------------------------------------------
# 1. 基础编译配置
# ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------
# 2. 显式定义源文件列表 (基于你提供的列表)
# ---------------------------------------------------------
set(SOURCE_FILES
        pch.h
        pch.cpp
        src/main.cpp
        include/graph/GeoGraph.h
        src/graph/GeoGraph.cpp
        include/graph/GeoFactory.h
        src/graph/GeoFactory.cpp
        include/graph/GeoSolver.h
        src/graph/GeoSolver.cpp


        include/plot/plotCall.h
        src/plot/plotCall.cpp
        include/plot/plotExplicit.h
        src/plot/plotExplicit.cpp
        include/plot/plotImplicit.h
        src/plot/plotImplicit.cpp

        include/plot/plotLine.h
        src/plot/plotLine.cpp
        include/plot/plotCircle.h
        src/plot/plotCircle.cpp


        include/CAS/RPN/RPN.h
        src/CAS/RPN/RPN.cpp
        include/CAS/RPN/MultiIntervalRPN.h
        include/interval/interval.h
        src/interval/intervalEvaluate.cpp
        include/interval/MultiInterval.h
        include/functions/functions.h  # 注意：Linux 区分大小写，确保路径一致
        include/functions/lerp.h

        include/CAS/RPN/ShuntingYard.h
        src/CAS/RPN/ShuntingYard.cpp
        src/graph/GeoCommands.cpp
        include/graph/GeoCommands.h
        src/graph/interact/GeoInteract.cpp
        include/graph/interact/GeoInteract.h
        src/grids/grids.cpp
        include/grids/grids.h
        src/graph/interact/preview/line/previewSegment.cpp
        include/graph/interact/preview/line/previewSegment.h
        src/graph/interact/preview/circle/circle_2points.cpp
        include/graph/interact/preview/circle/circle_2points.h
        include/graph/interact/preview/circle/circle_3points.h
        src/graph/interact/preview/circle/circle_3points.cpp
        include/graph/interact/preview/circle/circle_1point_1radius.h
        src/graph/interact/preview/circle/circle_1point_1radius.cpp
        include/graph/interact/preview/circle/circle_distance.h
        src/graph/interact/preview/circle/circle_distance.cpp
        include/graph/interact/preview/line/previewLine.h
        src/graph/interact/preview/line/previewLine.cpp
        include/graph/interact/preview/line/previewRay.h
        src/graph/interact/preview/line/previewRay.cpp


)

# ---------------------------------------------------------
# 3. 外部依赖配置 (Arch Linux 路径)
# ---------------------------------------------------------
set(GIAC_DIR "/home/giac")
# 注意：我们将包含路径指向 src/giac 这一级，这样 <giac/headers/gen.h> 能通
# 但为了支持 <giac/gen.h>，我们直接指向 headers 并配合软链接（见下文）
set(GIAC_INC_DIR "${GIAC_DIR}/src/giac/headers")
set(GIAC_LIB "${GIAC_DIR}/build/libjavagiac.so")

# 查找系统库
find_package(TBB REQUIRED)
find_package(xsimd REQUIRED)
find_package(Boost REQUIRED)
find_library(GMP_LIB gmp REQUIRED)
find_library(MPFR_LIB mpfr REQUIRED)
find_library(SIMDJSON_LIB simdjson REQUIRED)

# ---------------------------------------------------------
# 4. 创建执行文件
# ---------------------------------------------------------
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 配置预编译头文件 (PCH) - 大幅提升编译速度
# CMake 会自动处理 pch.h 和 pch.cpp
target_precompile_headers(${PROJECT_NAME} PRIVATE pch.h)

# ---------------------------------------------------------
# 5. 头文件、宏与选项
# ---------------------------------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${GIAC_INC_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        GIAC_GGB
        IN_GIAC
        HAVE_LIBMPFR
        SIZEOF_VOID_P=8
        GIAC_GENERIC_CONSTANTS
        SMARTPTR64
)

target_compile_options(${PROJECT_NAME} PRIVATE
        -march=native
        -O3
        -fpermissive

)

# ---------------------------------------------------------
# 6. 链接
# ---------------------------------------------------------
target_link_libraries(${PROJECT_NAME} PRIVATE
        TBB::tbb
        xsimd
        ${GIAC_LIB}
        ${MPFR_LIB}
        ${GMP_LIB}
        ${SIMDJSON_LIB}
)

# 配置运行时搜索路径，保证 .so 能被找到
set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${GIAC_DIR}/build"
)

# ---------------------------------------------------------
# 10. (可选) 打印调试信息，方便排查路径错误
# ---------------------------------------------------------
message(STATUS "GIAC Headers: ${GIAC_INCLUDE_DIR}")
message(STATUS "GIAC Lib: ${GIAC_LIBRARY}")