// stucanvas/utils/ffi_function.hpp
#pragma once

#include <utility>
#include <new>
#include <type_traits>
#include <functional>

namespace StuCanvas::utils
{
    template <typename Signature>
    class FfiFunction;

    // =========================================================================
    // 🚀 C++23 极致优化版 8 字节 FFI 闭包包装器 (Move-Only / Single-Allocation)
    // =========================================================================
    template <typename Ret, typename... Args>
    class [[nodiscard]] FfiFunction<Ret(Args...)>
    {
    public:
        using InvokerFn = Ret(*)(void*, Args&&...);
        using DeleterFn = void(*)(void*);

        // 💡 100% C-ABI 兼容的标准结构体布局
        struct Block
        {
            void* context;      // 指向闭包实际内存块的物理指针
            InvokerFn invoker;  // 静态调用函数指针
            DeleterFn deleter;  // 静态析构函数指针
        };

        // 默认构造与空构造
        constexpr FfiFunction() noexcept : block_(nullptr) {}
        constexpr FfiFunction(std::nullptr_t) noexcept : block_(nullptr) {}

        // 💡 核心优化 1：单次堆分配联合体（Single-Allocation Pattern）
        // 将控制块 Metadata 和用户 Lambda 闭包合并分配在同一块连续内存中，
        // 从而将 Heap Allocation 减少为正好 1 次，并达成极致的 L1 Cache 局部性。
        template <typename F>
        struct CombinedBlock
        {
            Block block;
            std::decay_t<F> functor;
        };

        // 构造函数：接受任意可调用对象（Lambda, 仿函数，Functor）
        template <typename F>
            requires (!std::is_same_v<std::decay_t<F>, FfiFunction> && std::is_invocable_r_v<Ret, F, Args...>)
        FfiFunction(F&& f)
        {
            using DecayedF = std::decay_t<F>;

            // 仅仅进行一次 new 分配
            auto* cb = new CombinedBlock<DecayedF>{
                .block = {
                    .context = nullptr, // 稍后绑定为 cb 自身
                    .invoker = [](void* ctx, Args&&... args) -> Ret {
                        // 寄存器直接通过偏移量直达 Lambda 闭包，0 查表开销
                        auto* self = static_cast<CombinedBlock<DecayedF>*>(ctx);
                        return self->functor(std::forward<Args>(args)...);
                    },
                    .deleter = [](void* ctx) {
                        delete static_cast<CombinedBlock<DecayedF>*>(ctx);
                    }
                },
                .functor = std::forward<F>(f)
            };

            // 绑定上下文为分配块的基地址
            cb->block.context = static_cast<void*>(cb);
            block_ = &(cb->block);
        }

        // 析构函数
        ~FfiFunction() noexcept { reset(); }

        // 💡 核心优化 2：移动唯一性（Move-Only）
        // 彻底禁掉拷贝，防止捕获了 std::unique_ptr 等独占资源的 Lambda 被非法复制导致崩溃
        FfiFunction(const FfiFunction&) = delete;
        FfiFunction& operator=(const FfiFunction&) = delete;

        // 移动构造
        FfiFunction(FfiFunction&& other) noexcept : block_(other.block_)
        {
            other.block_ = nullptr;
        }

        // 移动赋值
        FfiFunction& operator=(FfiFunction&& other) noexcept
        {
            if (this != &other) {
                reset();
                block_ = other.block_;
                other.block_ = nullptr;
            }
            return *this;
        }

        // 调用重载
        Ret operator()(Args... args) const
        {
            if (!block_) [[unlikely]] {
                throw std::bad_function_call();
            }
            return block_->invoker(block_->context, std::forward<Args>(args)...);
        }

        explicit operator bool() const noexcept { return block_ != nullptr; }

        // 💡 FFI 核心接口：剥离控制权并交付给外部语言
        // 外部语言拿到的 Block* 本质就是一个 8 字节的物理指针（句柄）
        [[nodiscard]] Block* release() noexcept
        {
            Block* temp = block_;
            block_ = nullptr;
            return temp;
        }

        // 重新接管外部返回的 FFI 闭包
        void acquire(Block* block) noexcept
        {
            reset();
            block_ = block;
        }

        void reset() noexcept
        {
            if (block_) {
                if (block_->deleter && block_->context) {
                    block_->deleter(block_->context);
                }
                block_ = nullptr;
            }
        }

    private:
        Block* block_ = nullptr; // 💡 物理大小：永远正好是 8 字节！
    };
} // namespace StuCanvas::utils