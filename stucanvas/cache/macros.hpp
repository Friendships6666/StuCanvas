#pragma once
#include "processor.hpp"
#include "hash.hpp"

/**
 * @brief STU_CACHE_BLOCK 核心宏
 * 设计原理：
 * 1. 利用 for 循环初始化 BlockProcessor 运行环境。
 * 2. _ctx.success 标记确保只有代码块顺利运行（未报错退出）时才触发 Save。
 * 3. 使用 if 分支实现对大括号代码块的“闪现跳过”。
 */
#define STU_CACHE_BLOCK(LABEL, HASH_VAL, ...) \
for (struct { \
StuCanvas::cache::BlockProcessor proc; \
bool done = false; \
bool success = false; \
} _ctx = { {LABEL, HASH_VAL} }; \
!_ctx.done; \
_ctx.done = true, (_ctx.success ? _ctx.proc.Save() : (void)0)) \
if ((_ctx.proc.BindAll(__VA_ARGS__), _ctx.proc.TryLoad())) { \
/* 缓存命中：此分支为空，自动跳过之后紧跟的大括号块 */ \
} else \
/* 缓存缺失：进入此循环执行用户代码，并在结束后标记 success */ \
for (int _once = 0; _once < 1; _once++, _ctx.success = true)

/**
 * @brief 输入指纹宏：将所有影响计算结果的参数进行哈希聚合
 */
#define STU_IN(...) StuCanvas::cache::HashTool::compute(__VA_ARGS__)

/**
 * @brief 输出列表宏：明确列出需要被缓存和还原的变量名
 */
#define STU_OUT(...) __VA_ARGS__