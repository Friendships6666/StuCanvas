#include "block_deque.hpp" // 引入你的 BlockDeque [1.2.9]
#include "tiny_vector.hpp" // 引入你的 TinyVector [1.2.9]
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// 跨平台 L1 缓存行预取函数封装 [1.2.9]
inline void prefetch_l1(const void *addr) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __builtin_prefetch(addr, 0, 3); // GCC/Clang 预取 [1.1.8]
#else
  _mm_prefetch(static_cast<const char *>(addr),
               _MM_HINT_T0); // MSVC 预取 [1.1.3]
#endif
}

// =====================================================================
// 1. 重构业务对象（大小为 256 字节，跨越 4 个缓存行）
// =====================================================================
struct MyObject {
  uint32_t id;           // 1. 第一个成员变量，占用 4 字节
  uint32_t padding[61];  // 2. 244 字节填充（244 + 4 = 248 字节）
  uint64_t target_value; // 3. 最后一个成员变量，占用 8 字节（8 字节对齐，248 +
                         // 8 = 256 字节）
};
static_assert(sizeof(MyObject) == 256, "MyObject must be exactly 256 bytes!");

// =====================================================================
// 2. 高度特化的 8 字节外壳 uint32_t 连续容器
// =====================================================================
class SpecializedIdVector {
private:
  struct Header {
    uint32_t size;
    uint32_t capacity;
  };
  static constexpr size_t HeaderOffset = 64;

  uint32_t *m_data = nullptr;

  [[nodiscard]] inline Header *get_header() const noexcept {
    if (!m_data)
      return nullptr;
    return reinterpret_cast<Header *>(reinterpret_cast<char *>(m_data) -
                                      HeaderOffset);
  }

public:
  SpecializedIdVector() noexcept = default;

  ~SpecializedIdVector() noexcept {
    if (m_data) {
      void *raw = reinterpret_cast<char *>(m_data) - HeaderOffset;
#ifdef _MSC_VER
      _aligned_free(raw);
#else
      std::free(raw);
#endif
    }
  }

  void reserve(uint32_t new_cap) {
    Header *h = get_header();
    uint32_t cur_cap = h ? h->capacity : 0;
    if (new_cap <= cur_cap)
      return;

    uint32_t sz = h ? h->size : 0;
    size_t total_size =
        HeaderOffset + static_cast<size_t>(new_cap) * sizeof(uint32_t);

    void *raw = nullptr;
#ifdef _MSC_VER
    raw = _aligned_malloc(total_size, 64);
#else
    if (posix_memalign(&raw, 64, total_size) != 0)
      throw std::bad_alloc();
#endif

    Header *new_h = reinterpret_cast<Header *>(raw);
    new_h->capacity = new_cap;
    new_h->size = sz;

    uint32_t *new_data = reinterpret_cast<uint32_t *>(
        reinterpret_cast<char *>(raw) + HeaderOffset);
    if (m_data) {
      std::memcpy(new_data, m_data, sz * sizeof(uint32_t));
      void *old_raw = reinterpret_cast<char *>(m_data) - HeaderOffset;
#ifdef _MSC_VER
      _aligned_free(old_raw);
#else
      std::free(old_raw);
#endif
    }
    m_data = new_data;
  }

  inline void push_back(uint32_t val) {
    Header *h = get_header();
    if (!h || h->size >= h->capacity) {
      uint32_t cur_sz = h ? h->size : 0;
      reserve(cur_sz == 0 ? 8 : cur_sz * 2);
      h = get_header();
    }
    m_data[h->size++] = val;
  }

  [[nodiscard]] inline uint32_t size() const noexcept {
    Header *h = get_header();
    return h ? h->size : 0;
  }

  [[nodiscard]] inline const uint32_t *__restrict data() const noexcept {
    return m_data;
  }

  inline uint32_t operator[](size_t idx) const noexcept { return m_data[idx]; }
};

// =====================================================================
// 3. 运行测试逻辑
// =====================================================================
void run_benchmark(size_t object_count, bool shuffle_access) {
  std::cout << "--------------------------------------------------\n";
  std::cout << "测试对象总数: " << object_count << " | 访问顺序: "
            << (shuffle_access ? "乱序(模拟真实堆碎片)" : "顺序(完美缓存)")
            << "\n";

  // 实例化连续的物理对象池
  std::vector<MyObject> pool(object_count);
  for (size_t i = 0; i < object_count; ++i) {
    pool[i].id = static_cast<uint32_t>(i);
    pool[i].target_value = i;
  }

  // 生成访问索引顺序
  std::vector<size_t> access_indices(object_count);
  std::iota(access_indices.begin(), access_indices.end(), 0);
  if (shuffle_access) {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(access_indices.begin(), access_indices.end(), g);
  }

  // 方案 A：BlockDeque 存裸指针 [3.2]
  StuCanvas::utils::BlockDeque<MyObject *, 512> deque_a;
  for (size_t idx : access_indices) {
    deque_a.push_back(&pool[idx]);
  }

  // 方案 B：vector 存 uint32_t ID
  std::vector<uint32_t> vector_b;
  vector_b.reserve(object_count);
  for (size_t idx : access_indices) {
    vector_b.push_back(static_cast<uint32_t>(idx));
  }

  // 方案 C：TinyVector 存 uint32_t ID [3.1]
  StuCanvas::utils::TinyVector<uint32_t> vector_c;
  vector_c.reserve(static_cast<uint32_t>(object_count));
  for (size_t idx : access_indices) {
    vector_c.push_back(static_cast<uint32_t>(idx));
  }

  // 方案 D：高度特化的 SpecializedIdVector
  SpecializedIdVector vector_d;
  vector_d.reserve(static_cast<uint32_t>(object_count));
  for (size_t idx : access_indices) {
    vector_d.push_back(static_cast<uint32_t>(idx));
  }

  MyObject *pool_base = pool.data();

  // --------------------------------------------------
  // 【方案 A 测试】：BlockDeque<T*> 双重解引用 [3.2]
  // --------------------------------------------------
  {
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t sum_a = 0;
    size_t sz = deque_a.size();
    for (size_t i = 0; i < sz; ++i) {
      sum_a += deque_a[i]->target_value;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_a = end - start;
    // 🚀 修复：打印校验和，强行阻止编译器死代码消除
    std::cout << "[方案 A] (BlockDeque<T*>):             " << elapsed_a.count()
              << " ms | 校验和: " << sum_a << "\n";
  }

  // --------------------------------------------------
  // 【方案 B 测试】：vector<uint32_t> + pool[ID]
  // --------------------------------------------------
  {
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t sum_b = 0;
    size_t sz = vector_b.size();
    for (size_t i = 0; i < sz; ++i) {
      uint32_t id = vector_b[i];
      sum_b += pool_base[id].target_value;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_b = end - start;
    // 🚀 修复：打印校验和
    std::cout << "[方案 B] (std::vector<uint32_t>):       " << elapsed_b.count()
              << " ms | 校验和: " << sum_b << "\n";
  }

  // --------------------------------------------------
  // 【方案 C 测试】：TinyVector<uint32_t> + pool[ID] [3.1]
  // --------------------------------------------------
  {
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t sum_c = 0;
    size_t sz = vector_c.size();
    for (size_t i = 0; i < sz; ++i) {
      uint32_t id = vector_c[i];
      sum_c += pool_base[id].target_value;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_c = end - start;
    // 🚀 修复：打印校验和
    std::cout << "[方案 C] (TinyVector<uint32_t>):        " << elapsed_c.count()
              << " ms | 校验和: " << sum_c << "\n";
  }

  // --------------------------------------------------
  // 【方案 D 测试】：SpecializedIdVector + 软件主动预取热循环
  // --------------------------------------------------
  {
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t sum_d = 0;
    size_t sz = vector_d.size();
    const uint32_t *__restrict raw_ids = vector_d.data();

    // 软件预取跨度
    const size_t kPrefetchDistance = 24;

    for (size_t i = 0; i < sz; ++i) {
      if (i + kPrefetchDistance < sz) [[likely]] {
        uint32_t prefetch_id = raw_ids[i + kPrefetchDistance];
        prefetch_l1(&pool_base[prefetch_id].target_value);
      }
      uint32_t id = raw_ids[i];
      sum_d += pool_base[id].target_value;
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_d = end - start;
    std::cout << "[方案 D] (SpecializedIdVector+Prefetch): "
              << elapsed_d.count() << " ms | 校验和: " << sum_d << "\n";
  }
}

int main() {
  const size_t element_count = 10'000'000; // 1000 万个对象视图
  run_benchmark(element_count, false);
  run_benchmark(element_count, true);
  return 0;
}
