@echo off
setlocal

echo =======================================================
echo === Compiling WASMTest for Emscripten...
echo =======================================================

REM --- 1. 配置路径 (Configuration) ---
REM --- 我们只需要头文件。库文件将由 Emscripten 自己处理。---
set TBB_INCLUDE_DIR=D:\emsdk\upstream\emscripten\cache\sysroot\include

REM --- 这是您的 Emscripten C++ 编译器的路径 ---
set EMSCRIPTEN_CXX=D:\emsdk\upstream\emscripten\em++.bat


REM --- 2. 编译命令 (The Final Build Command) ---
REM --- 这个版本结合了手动提供头文件和 Emscripten 自动链接库的优点 ---
%EMSCRIPTEN_CXX% ^
    main.cpp ^
    -o WASMTest.html ^
    -std=c++20 ^
    -I"%TBB_INCLUDE_DIR%" ^
    -s USE_TBB=1 ^
    -s PTHREAD_POOL_SIZE=8 ^
    -s ALLOW_MEMORY_GROWTH=1

REM --- 3. 检查结果 (Check Result) ---
if errorlevel 1 (
    echo.
    echo **********************************
    echo *** BUILD FAILED! ***
    echo **********************************
    exit /b 1
) else (
    echo.
    echo =======================================================
    echo === BUILD SUCCESSFUL! ===
    echo === Output files: WASMTest.html, .js, .wasm
    echo =======================================================
)

endlocal