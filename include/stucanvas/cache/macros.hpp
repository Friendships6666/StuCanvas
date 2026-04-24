#pragma once
#include "processor.hpp"
#include "hash.hpp"

/**
 * @brief 核心宏：利用 BindAll 处理变长参数
 */
#define STU_CACHE_BLOCK(LABEL, HASH_VAL, ...) \
for (struct { \
StuCanvas::cache::BlockProcessor proc; \
bool done = false; \
} _ctx = { {LABEL, HASH_VAL} }; \
!_ctx.done; \
_ctx.done = true) \
if ((_ctx.proc.BindAll(__VA_ARGS__), _ctx.proc.TryLoad())) { \
/* 命中：跳过大括号 */ \
} else \
/* 失效：执行后保存 */ \
for (int _once = 0; _once < 1; _once++, _ctx.proc.Save())

#define STU_IN(...) StuCanvas::cache::HashTool::compute(__VA_ARGS__)
#define STU_OUT(...) __VA_ARGS__