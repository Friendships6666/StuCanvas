#ifndef STUVM_ENGINE_HPP
#define STUVM_ENGINE_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <cstring>
#include <memory>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

// 引入 LLVM 高性能 ADT 容器
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>

// 引入 XSIMD
#include <xsimd/xsimd.hpp>

namespace StuVM {

// ============================================================================
// [0] SIMD 宽度抽象层
// ============================================================================
#ifdef STUVM_SIMD_COMPAT
    using batch_part_t = xsimd::make_sized_batch_t<double, 2>;
    struct alignas(64) simdv_t {
        batch_part_t lo;
        batch_part_t hi;
    };
#else
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
// [2] MMU: 64位多级页表内存管理单元 (4-Level Page Table)
// ============================================================================
constexpr uint64_t PAGE_SIZE = 4096;
constexpr uint64_t PAGE_MASK = 0xFFF;

class MMU {
private:
    struct Page {
        alignas(64) uint8_t data[PAGE_SIZE]{};
        uint8_t perms; // 权限位：1=R, 2=W, 4=X
        Page(uint8_t p) : perms(p) { std::memset(data, 0, PAGE_SIZE); }
    };

    struct L3Table { Page* entries[512] = {nullptr}; ~L3Table() { for(auto p:entries) delete p; } };
    struct L2Table { L3Table* entries[512] = {nullptr}; ~L2Table() { for(auto p:entries) delete p; } };
    struct L1Table { L2Table* entries[512] = {nullptr}; ~L1Table() { for(auto p:entries) delete p; } };
    struct L0Table { L1Table* entries[512] = {nullptr}; ~L0Table() { for(auto p:entries) delete p; } };

    L0Table root;
    std::shared_mutex rw_mtx;

public:
    // 映射或修改内存页权限
    void map_page(uint64_t vaddr, uint8_t perms) {
        std::unique_lock<std::shared_mutex> lock(rw_mtx);
        uint64_t l0 = (vaddr >> 39) & 0x1FF;
        uint64_t l1 = (vaddr >> 30) & 0x1FF;
        uint64_t l2 = (vaddr >> 21) & 0x1FF;
        uint64_t l3 = (vaddr >> 12) & 0x1FF;

        if (!root.entries[l0]) root.entries[l0] = new L1Table();
        if (!root.entries[l0]->entries[l1]) root.entries[l0]->entries[l1] = new L2Table();
        if (!root.entries[l0]->entries[l1]->entries[l2]) root.entries[l0]->entries[l1]->entries[l2] = new L3Table();

        if (!root.entries[l0]->entries[l1]->entries[l2]->entries[l3]) {
            root.entries[l0]->entries[l1]->entries[l2]->entries[l3] = new Page(perms);
        } else {
            root.entries[l0]->entries[l1]->entries[l2]->entries[l3]->perms = perms;
        }
    }

    // 映射一段连续内存区
    void map_region(uint64_t start_addr, size_t size, uint8_t perms) {
        uint64_t start = start_addr & ~PAGE_MASK;
        uint64_t end = (start_addr + size + PAGE_MASK) & ~PAGE_MASK;
        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            map_page(addr, perms);
        }
    }

    // 获取宿主机的直接指针 (用于原子操作或快速单页访问)
    uint8_t* get_host_ptr(uint64_t vaddr, bool is_write) {
        std::shared_lock<std::shared_mutex> lock(rw_mtx);
        uint64_t l0 = (vaddr >> 39) & 0x1FF;
        uint64_t l1 = (vaddr >> 30) & 0x1FF;
        uint64_t l2 = (vaddr >> 21) & 0x1FF;
        uint64_t l3 = (vaddr >> 12) & 0x1FF;

        if (!root.entries[l0] || !root.entries[l0]->entries[l1] ||
            !root.entries[l0]->entries[l1]->entries[l2]) return nullptr;

        Page* page = root.entries[l0]->entries[l1]->entries[l2]->entries[l3];
        if (!page) return nullptr;

        if (is_write && !(page->perms & 2)) return nullptr; // 无写权限
        if (!is_write && !(page->perms & 1)) return nullptr; // 无读权限

        return page->data + (vaddr & PAGE_MASK);
    }

    // 跨页安全读取
    bool read(uint64_t vaddr, void* dest, size_t size) {
        uint8_t* dst = static_cast<uint8_t*>(dest);
        size_t bytes_read = 0;
        while (bytes_read < size) {
            uint64_t current_vaddr = vaddr + bytes_read;
            size_t offset = current_vaddr & PAGE_MASK;
            size_t chunk = std::min(size - bytes_read, PAGE_SIZE - offset);

            uint8_t* host_ptr = get_host_ptr(current_vaddr, false);
            if (!host_ptr) return false;

            std::memcpy(dst + bytes_read, host_ptr, chunk);
            bytes_read += chunk;
        }
        return true;
    }

    // 跨页安全写入
    bool write(uint64_t vaddr, const void* src, size_t size) {
        const uint8_t* source = static_cast<const uint8_t*>(src);
        size_t bytes_written = 0;
        while (bytes_written < size) {
            uint64_t current_vaddr = vaddr + bytes_written;
            size_t offset = current_vaddr & PAGE_MASK;
            size_t chunk = std::min(size - bytes_written, PAGE_SIZE - offset);

            uint8_t* host_ptr = get_host_ptr(current_vaddr, true);
            if (!host_ptr) return false;

            std::memcpy(host_ptr, source + bytes_written, chunk);
            bytes_written += chunk;
        }
        return true;
    }
};

// ============================================================================
// [3] 全局系统主板
// ============================================================================
class System {
public:
    MMU mmu;

    std::mutex futex_mtx;
    llvm::DenseMap<uint64_t, std::condition_variable*> futex_queues;
    llvm::DenseMap<int, int> fd_map;
    std::atomic<int> next_tid{1000};
    std::atomic<bool> vm_running{true}; // 全局运行状态

    System() {
        // 预分配底层内存：为向后兼容，默认映射 0x0 ~ 128MB 区域 (RWX)
        // 实际使用中，推荐使用 mmu.map_region 动态映射真实的 ELF Sections
        mmu.map_region(0, 128 * 1024 * 1024, 7);

        // 映射一个默认的栈空间区 (例如 Linux 的 0x7FFFFFFF0000 起始的 16MB)
        mmu.map_region(0x7FFFFFF00000ull, 16 * 1024 * 1024, 7);
    }

    ~System() {
        for (auto& pair : futex_queues) delete pair.second;
    }
};

// ============================================================================
// [4] VCPU 核心实现
// ============================================================================
class VCPU {
public:
    int tid;
    System* sys;
    uint64_t regs[32] = {0};
    uint64_t pc = 0;

    alignas(64) simdv_t vregs[32]{};

    uint64_t reservation_addr = 0xFFFFFFFFFFFFFFFF;
    const std::vector<RuntimeInst>* code_cache = nullptr;
    const llvm::DenseMap<uint64_t, size_t>* addr_map = nullptr;

    VCPU(System* s, int id) : sys(s), tid(id) {}

    // ========================================================================
    // 软件级内存错误拦截器
    // ========================================================================
    void handle_mem_error(uint64_t fault_addr, bool is_write, const char* specific_reason = nullptr) {
        if (!sys || !sys->vm_running.load()) return;
        sys->vm_running = false; // 通知其他线程立即停止

        uint64_t sp = regs[2]; // RISC-V 栈指针

        std::cerr << "\n[STUVM FATAL EXCEPTION] CPU Thread " << tid << " Terminated.\n";

        if (specific_reason) {
            std::cerr << "Reason: " << specific_reason << "\n";
        } else if (fault_addr >= sp - 0x800000 && fault_addr <= sp) {
            // 启发式检测：越界访问在栈指针下方 8MB 以内，判定为栈溢出
            std::cerr << "Reason: Stack Overflow (Accessed unmapped guard page)\n";
        } else if (fault_addr < 0x1000) {
            std::cerr << "Reason: Null Pointer Dereference\n";
        } else {
            std::cerr << "Reason: Segmentation Fault (Invalid " << (is_write ? "Write" : "Read") << " Access)\n";
        }

        std::cerr << "Faulting Address: 0x" << std::hex << fault_addr << "\n";
        std::cerr << "Program Counter : 0x" << pc << "\n";
        std::cerr << "Stack Pointer   : 0x" << sp << std::dec << "\n\n";
    }

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

        #define DISPATCH() if(!sys->vm_running.load()) goto OP_EXIT; goto *dispatch_table[inst->op_idx]
        #define JUMP_TO(target) \
            { auto it = addr_map->find(target); \
              if (it != addr_map->end()) { inst = inst_base + it->second; DISPATCH(); } \
              else { goto OP_EXIT; } }

        DISPATCH();

    OP_ADDI: regs[inst->rd] = regs[inst->rs1] + inst->imm; regs[0] = 0; inst++; DISPATCH();
    OP_ADD:  regs[inst->rd] = regs[inst->rs1] + regs[inst->rs2]; regs[0] = 0; inst++; DISPATCH();

    OP_LD: {
        uint64_t addr = regs[inst->rs1] + inst->imm;
        if (!sys->mmu.read(addr, &regs[inst->rd], 8)) { handle_mem_error(addr, false); return; }
        inst++; DISPATCH();
    }
    OP_SD: {
        uint64_t addr = regs[inst->rs1] + inst->imm;
        if (!sys->mmu.write(addr, &regs[inst->rs2], 8)) { handle_mem_error(addr, true); return; }
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
        uint64_t addr = regs[inst->rs1];
        if (addr % 8 != 0) { handle_mem_error(addr, true, "Alignment Fault on AMO"); return; }
        uint8_t* host_ptr = sys->mmu.get_host_ptr(addr, true);
        if (!host_ptr) { handle_mem_error(addr, true); return; }

        std::atomic_ref<uint64_t> atomic_val(*reinterpret_cast<uint64_t*>(host_ptr));
        regs[inst->rd] = atomic_val.fetch_add(regs[inst->rs2], std::memory_order_seq_cst);
        regs[0] = 0; inst++; DISPATCH();
    }

    OP_LR: {
        uint64_t addr = regs[inst->rs1];
        if (addr % 8 != 0) { handle_mem_error(addr, false, "Alignment Fault on LR"); return; }
        reservation_addr = addr;
        if (!sys->mmu.read(addr, &regs[inst->rd], 8)) { handle_mem_error(addr, false); return; }
        inst++; DISPATCH();
    }

    OP_SC: {
        uint64_t addr = regs[inst->rs1];
        if (addr % 8 != 0) { handle_mem_error(addr, true, "Alignment Fault on SC"); return; }
        if (reservation_addr == addr) {
            if (!sys->mmu.write(addr, &regs[inst->rs2], 8)) { handle_mem_error(addr, true); return; }
            regs[inst->rd] = 0;
        } else {
            regs[inst->rd] = 1;
        }
        reservation_addr = 0xFFFFFFFFFFFFFFFF;
        inst++; DISPATCH();
    }

    // ========================================================================
    //[SIMD 执行逻辑 - 增加跨边界安全检查]
    // ========================================================================
    OP_VLE: {
        uint64_t addr = regs[inst->rs1];
        size_t v_size = sizeof(simdv_t);
        alignas(64) uint8_t tmp[64];

        // 检查向量是否跨越了 4096 字节的内存页边界
        if ((addr & PAGE_MASK) + v_size > PAGE_SIZE) {
            if (!sys->mmu.read(addr, tmp, v_size)) { handle_mem_error(addr, false); return; }
            double* src = reinterpret_cast<double*>(tmp);
#ifdef STUVM_SIMD_COMPAT
            vregs[inst->rd].lo = batch_part_t::load_unaligned(src);
            vregs[inst->rd].hi = batch_part_t::load_unaligned(src + 2);
#else
            vregs[inst->rd] = simdv_t::load_unaligned(src);
#endif
        } else {
            uint8_t* host_ptr = sys->mmu.get_host_ptr(addr, false);
            if (!host_ptr) { handle_mem_error(addr, false); return; }
            double* src = reinterpret_cast<double*>(host_ptr);
#ifdef STUVM_SIMD_COMPAT
            vregs[inst->rd].lo = batch_part_t::load_unaligned(src);
            vregs[inst->rd].hi = batch_part_t::load_unaligned(src + 2);
#else
            // 这里使用 unaligned 防止虚拟机传入非 32字节对齐的地址导致宿主机崩溃
            vregs[inst->rd] = simdv_t::load_unaligned(src);
#endif
        }
        inst++; DISPATCH();
    }

    OP_VSE: {
        uint64_t addr = regs[inst->rs1];
        size_t v_size = sizeof(simdv_t);
        alignas(64) uint8_t tmp[64];

        if ((addr & PAGE_MASK) + v_size > PAGE_SIZE) {
            double* dst = reinterpret_cast<double*>(tmp);
#ifdef STUVM_SIMD_COMPAT
            vregs[inst->rs2].lo.store_unaligned(dst);
            vregs[inst->rs2].hi.store_unaligned(dst + 2);
#else
            vregs[inst->rs2].store_unaligned(dst);
#endif
            if (!sys->mmu.write(addr, tmp, v_size)) { handle_mem_error(addr, true); return; }
        } else {
            uint8_t* host_ptr = sys->mmu.get_host_ptr(addr, true);
            if (!host_ptr) { handle_mem_error(addr, true); return; }
            double* dst = reinterpret_cast<double*>(host_ptr);
#ifdef STUVM_SIMD_COMPAT
            vregs[inst->rs2].lo.store_unaligned(dst);
            vregs[inst->rs2].hi.store_unaligned(dst + 2);
#else
            vregs[inst->rs2].store_unaligned(dst);
#endif
        }
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
        if(!sys || !sys->vm_running.load()) return;
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
            case 56: { // sys_openat
                uint64_t path_ptr = regs[11];
                std::string path;
                char c;
                while (true) {
                    if (!sys->mmu.read(path_ptr++, &c, 1)) { handle_mem_error(path_ptr - 1, false); return; }
                    if (c == '\0') break;
                    path += c;
                }

                int flags = static_cast<int>(regs[12]);
                int host_fd = open(path.c_str(), flags, 0666);
                if (host_fd >= 0) {
                    int guest_fd = host_fd + 10;
                    sys->fd_map[guest_fd] = host_fd;
                    regs[10] = guest_fd;
                } else {
                    regs[10] = -1;
                }
                break;
            }
            case 63: { // sys_read
                int guest_fd = static_cast<int>(regs[10]);
                uint64_t buf = regs[11];
                size_t count = regs[12];
                if (sys->fd_map.count(guest_fd)) {
                    std::vector<uint8_t> tmp(count);
                    ssize_t bytes_read = read(sys->fd_map[guest_fd], tmp.data(), count);
                    if (bytes_read > 0) {
                        if (!sys->mmu.write(buf, tmp.data(), bytes_read)) { handle_mem_error(buf, true); return; }
                    }
                    regs[10] = bytes_read;
                } else {
                    regs[10] = -1;
                }
                break;
            }
            case 64: { // sys_write
                int fd = static_cast<int>(regs[10]);
                uint64_t buf = regs[11];
                uint64_t len = regs[12];
                std::vector<uint8_t> tmp(len);

                if (!sys->mmu.read(buf, tmp.data(), len)) { handle_mem_error(buf, false); return; }

                if (fd == 1 || fd == 2) {
                    std::cout << "[Core " << tid << "] " << std::string(reinterpret_cast<char*>(tmp.data()), len);
                    regs[10] = len;
                } else if (sys->fd_map.count(fd)) {
                    regs[10] = write(sys->fd_map[fd], tmp.data(), len);
                }
                break;
            }
            case 220: { // sys_clone
                uint64_t child_sp = regs[11];
                uint64_t child_pc = pc + 4;

                int new_tid = sys->next_tid.fetch_add(1);
                auto child_cpu = std::make_unique<VCPU>(sys, new_tid);
                std::memcpy(child_cpu->regs, this->regs, sizeof(regs));
                child_cpu->code_cache = this->code_cache;
                child_cpu->addr_map = this->addr_map;

                child_cpu->regs[10] = 0;
                child_cpu->regs[2]  = child_sp;

                std::thread([c_cpu = std::move(child_cpu), child_pc]() mutable {
                    c_cpu->run(child_pc);
                }).detach();

                regs[10] = new_tid;
                break;
            }
            case 98: { // sys_futex
                uint64_t uaddr = regs[10];
                if (uaddr % 4 != 0) { handle_mem_error(uaddr, false, "Unaligned Futex Address"); return; }

                int op = regs[11] & ~128;
                auto val = static_cast<uint32_t>(regs[12]);

                std::unique_lock<std::mutex> lock(sys->futex_mtx);
                if (op == 0) { // WAIT
                    uint8_t* host_ptr = sys->mmu.get_host_ptr(uaddr, false);
                    if (!host_ptr) { handle_mem_error(uaddr, false); return; }

                    if (*reinterpret_cast<uint32_t*>(host_ptr) == val) {
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
            case 93: case 94: sys->vm_running = false; break; // Exit
            default: break;
        }
    }
};

} // namespace StuVM
#endif