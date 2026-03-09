#ifndef STUVM_ENGINE_HPP
#define STUVM_ENGINE_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <memory>
#include <fcntl.h>
#include <unistd.h>

// 引入 LLVM 高性能 ADT 容器
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>

// 引入 XSIMD
#include <xsimd/xsimd.hpp>

namespace StuVM {

// ============================================================================
// [0] SIMD 宽度抽象层 (支持兼容模式)
// ============================================================================
#ifdef STUVM_SIMD_COMPAT
    // 兼容模式：使用 128 位宽度 (2个 double)
    using batch_part_t = xsimd::make_sized_batch_t<double, 2>;
    struct alignas(64) simdv_t {
        batch_part_t lo;
        batch_part_t hi;
    };
#else
    // 标准模式：使用 256 位宽度 (4个 double)
    using simdv_t = xsimd::make_sized_batch_t<double, 4>;
#endif

// ============================================================================
// [1] 系统指令索引
// ============================================================================
enum OpIndex : uint8_t {
    IDX_UNKNOWN = 0, IDX_ADDI, IDX_ADD, IDX_LD, IDX_SD,
    IDX_JAL, IDX_JALR, IDX_BEQ, IDX_BNE,
    IDX_AMOADD, IDX_LR, IDX_SC,
    IDX_VADD_VV, IDX_VLE, IDX_VSE,
    IDX_ECALL, IDX_EXIT
};

struct RuntimeInst {
    OpIndex op_idx;
    uint8_t rd, rs1, rs2;
    int64_t imm;
    uint64_t addr;
};

// ============================================================================
// [2] 全局系统主板 (带虚拟磁盘支持)
// ============================================================================
class System {
public:
    static constexpr uint64_t MEM_SIZE = 128 * 1024 * 1024;
    alignas(64) uint8_t* ram;

    std::mutex futex_mtx;
    llvm::DenseMap<uint64_t, std::condition_variable*> futex_queues; // 使用指针管理 CV

    // 虚拟磁盘句柄映射：Guest FD -> Host FD
    // 使用 DenseMap 提升查找速度
    llvm::DenseMap<int, int> fd_map;
    std::atomic<int> next_tid{1000};

    System() {
        // 64 字节对齐分配，预留给未来 512 位指令
        size_t alloc_size = (MEM_SIZE + 63) & ~63;
        ram = static_cast<uint8_t*>(std::aligned_alloc(64, alloc_size));
        std::memset(ram, 0, MEM_SIZE);
    }

    ~System() {
        std::free(ram);
        for (auto& pair : futex_queues) delete pair.second;
    }

    inline uint64_t mask_addr(uint64_t addr) const { return addr % MEM_SIZE; }
};

// ============================================================================
// [3] VCPU 核心实现
// ============================================================================
class VCPU {
public:
    int tid;
    System* sys;
    uint64_t regs[32] = {0};
    uint64_t pc = 0;

    alignas(64) simdv_t vregs[32];

    uint64_t reservation_addr = 0xFFFFFFFFFFFFFFFF;
    const std::vector<RuntimeInst>* code_cache = nullptr;
    const llvm::DenseMap<uint64_t, size_t>* addr_map = nullptr;

    VCPU(System* s, int id) : sys(s), tid(id) {}

    void run(uint64_t start_pc) {
        if (!code_cache || !addr_map || addr_map->find(start_pc) == addr_map->end()) return;

        pc = start_pc;
        const RuntimeInst* inst_base = code_cache->data();
        const RuntimeInst* inst = inst_base + addr_map->lookup(pc);

        static const void* dispatch_table[] = {
            &&OP_UNKNOWN, &&OP_ADDI, &&OP_ADD, &&OP_LD, &&OP_SD,
            &&OP_JAL, &&OP_JALR, &&OP_BEQ, &&OP_BNE,
            &&OP_AMOADD, &&OP_LR, &&OP_SC,
            &&OP_VADD_VV, &&OP_VLE, &&OP_VSE,
            &&OP_ECALL, &&OP_EXIT
        };

        #define DISPATCH() goto *dispatch_table[inst->op_idx]
        #define JUMP_TO(target) \
            { auto it = addr_map->find(target); \
              if (it != addr_map->end()) { inst = inst_base + it->second; DISPATCH(); } \
              else { goto OP_EXIT; } }

        DISPATCH();

    OP_ADDI: regs[inst->rd] = regs[inst->rs1] + inst->imm; regs[0] = 0; inst++; DISPATCH();
    OP_ADD:  regs[inst->rd] = regs[inst->rs1] + regs[inst->rs2]; regs[0] = 0; inst++; DISPATCH();

    OP_LD: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1] + inst->imm);
        std::memcpy(&regs[inst->rd], &sys->ram[addr], 8);
        inst++; DISPATCH();
    }
    OP_SD: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1] + inst->imm);
        std::memcpy(&sys->ram[addr], &regs[inst->rs2], 8);
        inst++; DISPATCH();
    }

    OP_JAL:
        if (inst->rd != 0) regs[inst->rd] = inst->addr + 4;
        JUMP_TO(inst->addr + inst->imm);

    OP_JALR: {
        uint64_t target = regs[inst->rs1] + inst->imm;
        if (inst->rd != 0) regs[inst->rd] = inst->addr + 4;
        JUMP_TO(target);
    }

    OP_BEQ:  if (regs[inst->rs1] == regs[inst->rs2]) { JUMP_TO(inst->addr + inst->imm); } else { inst++; DISPATCH(); }
    OP_BNE:  if (regs[inst->rs1] != regs[inst->rs2]) { JUMP_TO(inst->addr + inst->imm); } else { inst++; DISPATCH(); }

    OP_AMOADD: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1]);
        std::atomic_ref<uint64_t> atomic_val(*reinterpret_cast<uint64_t*>(&sys->ram[addr]));
        regs[inst->rd] = atomic_val.fetch_add(regs[inst->rs2], std::memory_order_seq_cst);
        regs[0] = 0; inst++; DISPATCH();
    }

    OP_LR: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1]);
        reservation_addr = addr;
        std::memcpy(&regs[inst->rd], &sys->ram[addr], 8);
        inst++; DISPATCH();
    }

    OP_SC: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1]);
        if (reservation_addr == addr) {
            std::memcpy(&sys->ram[addr], &regs[inst->rs2], 8);
            regs[inst->rd] = 0;
        } else {
            regs[inst->rd] = 1;
        }
        reservation_addr = 0xFFFFFFFFFFFFFFFF;
        inst++; DISPATCH();
    }

    // ========================================================================
    // [SIMD 执行逻辑 - 支持兼容模式]
    // ========================================================================
    OP_VLE: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1]);
        double* src = reinterpret_cast<double*>(&sys->ram[addr]);
#ifdef STUVM_SIMD_COMPAT
        vregs[inst->rd].lo = batch_part_t::load_unaligned(src);
        vregs[inst->rd].hi = batch_part_t::load_unaligned(src + 2);
#else
        vregs[inst->rd] = simdv_t::load_aligned(src);
#endif
        inst++; DISPATCH();
    }

    OP_VSE: {
        uint64_t addr = sys->mask_addr(regs[inst->rs1]);
        double* dst = reinterpret_cast<double*>(&sys->ram[addr]);
#ifdef STUVM_SIMD_COMPAT
        vregs[inst->rs2].lo.store_unaligned(dst);
        vregs[inst->rs2].hi.store_unaligned(dst + 2);
#else
        vregs[inst->rs2].store_aligned(dst);
#endif
        inst++; DISPATCH();
    }

    OP_VADD_VV: {
#ifdef STUVM_SIMD_COMPAT
        vregs[inst->rd].lo = vregs[inst->rs1].lo + vregs[inst->rs2].lo;
        vregs[inst->rd].hi = vregs[inst->rs1].hi + vregs[inst->rs2].hi;
#else
        vregs[inst->rd] = vregs[inst->rs1] + vregs[inst->rs2];
#endif
        inst++; DISPATCH();
    }

    OP_ECALL:
        handle_syscall();
        if(!sys) return;
        inst++; DISPATCH();

    OP_UNKNOWN: inst++; DISPATCH();
    OP_EXIT: return;

    #undef DISPATCH
    #undef JUMP_TO
    }

private:
    void handle_syscall() {
        uint64_t sys_no = regs[17];

        switch (sys_no) {
            case 56: { // sys_openat (虚拟磁盘支持)
                uint64_t path_ptr = sys->mask_addr(regs[11]);
                const char* path = reinterpret_cast<const char*>(&sys->ram[path_ptr]);
                int flags = static_cast<int>(regs[12]);
                // 在 WASM/EMCC 下，该调用会映射到虚拟文件系统
                int host_fd = open(path, flags, 0666);
                if (host_fd >= 0) {
                    int guest_fd = host_fd + 10; // 防止冲突
                    sys->fd_map[guest_fd] = host_fd;
                    regs[10] = guest_fd;
                } else {
                    regs[10] = -1;
                }
                break;
            }
            case 63: { // sys_read
                int guest_fd = static_cast<int>(regs[10]);
                uint64_t buf = sys->mask_addr(regs[11]);
                size_t count = regs[12];
                if (sys->fd_map.count(guest_fd)) {
                    regs[10] = read(sys->fd_map[guest_fd], &sys->ram[buf], count);
                } else {
                    regs[10] = -1;
                }
                break;
            }
            case 64: { // sys_write
                int fd = static_cast<int>(regs[10]);
                uint64_t buf = sys->mask_addr(regs[11]);
                uint64_t len = regs[12];
                if (fd == 1 || fd == 2) {
                    std::cout << "[Core " << tid << "] " << std::string(reinterpret_cast<char*>(&sys->ram[buf]), len);
                    regs[10] = len;
                } else if (sys->fd_map.count(fd)) {
                    regs[10] = write(sys->fd_map[fd], &sys->ram[buf], len);
                }
                break;
            }
            case 220: { // sys_clone
                [[maybe_unused]] uint64_t flags = regs[10];
                uint64_t child_sp = regs[11];
                uint64_t child_pc = pc + 4; // 修复：在 lambda 内部被使用

                int new_tid = sys->next_tid.fetch_add(1);
                auto child_cpu = std::make_unique<VCPU>(sys, new_tid);
                std::memcpy(child_cpu->regs, this->regs, sizeof(regs));
                child_cpu->code_cache = this->code_cache;
                child_cpu->addr_map = this->addr_map;

                child_cpu->regs[10] = 0;
                child_cpu->regs[2]  = child_sp;

                std::thread([c_cpu = std::move(child_cpu), child_pc]() mutable {
                    c_cpu->run(child_pc); // 显式使用 child_pc
                }).detach();

                regs[10] = new_tid;
                break;
            }
            case 98: { // sys_futex
                uint64_t uaddr = sys->mask_addr(regs[10]);
                int op = regs[11] & ~128;
                auto val = static_cast<uint32_t>(regs[12]);

                std::unique_lock<std::mutex> lock(sys->futex_mtx);
                if (op == 0) { // WAIT
                    if (*reinterpret_cast<uint32_t*>(&sys->ram[uaddr]) == val) {
                        if (sys->futex_queues.find(uaddr) == sys->futex_queues.end())
                            sys->futex_queues[uaddr] = new std::condition_variable();
                        sys->futex_queues[uaddr]->wait(lock);
                    }
                    regs[10] = 0;
                } else if (op == 1) { // WAKE
                    if (sys->futex_queues.count(uaddr)) sys->futex_queues[uaddr]->notify_all();
                    regs[10] = val;
                }
                break;
            }
            case 93: case 94: sys = nullptr; break;
            default: break;
        }
    }
};

} // namespace StuVM
#endif