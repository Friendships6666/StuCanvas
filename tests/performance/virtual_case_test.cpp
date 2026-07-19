// benchmark.cpp
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>
#include <cmath>

#define NOINLINE inline

// =========================================================================
// 💡 全局常数与类型声明
// =========================================================================
constexpr size_t OBJECT_COUNT = 1'000'000; // 100 万个元素
constexpr size_t ITERATIONS = 10;          // 10 轮测试

enum class ObjectType : uint8_t { 
  TypeA, TypeB, TypeC, TypeD, TypeE, TypeF,
  TypeG, TypeH, TypeI, TypeJ, TypeK, TypeL 
};

// 极致复杂的数学函数
inline float math_core(float val, int loops, float c1, float c2, float c3, float c4) noexcept {
    float res = val;
    for (int i = 0; i < loops; ++i) {
        if (res > 10.0f) {
            res = std::sin(res) * c1 + c2;
        } else {
            res = std::cos(res) * c3 - c4;
        }
    }
    return res;
}

// =========================================================================
// 🚀 方案一：C风格虚函数表 (VTable Interface) 指针方案
// =========================================================================
struct ISolverVTable {
  void (*solve)(void *self, float val);
};

struct BaseObject {
  const ISolverVTable *vptr = nullptr;
  ObjectType type;
  float value = 0.0f;
  float init_val = 0.0f;
};

struct ObjectA : public BaseObject {};
struct ObjectB : public BaseObject {};
struct ObjectC : public BaseObject {};
struct ObjectD : public BaseObject {};
struct ObjectE : public BaseObject {};
struct ObjectF : public BaseObject {};
struct ObjectG : public BaseObject {};
struct ObjectH : public BaseObject {};
struct ObjectI : public BaseObject {};
struct ObjectJ : public BaseObject {};
struct ObjectK : public BaseObject {};
struct ObjectL : public BaseObject {};

NOINLINE void solve_A(void *self, float val) { static_cast<ObjectA *>(self)->value += math_core(val, 15, 1.1f, 0.5f, 0.9f, 0.2f); }
NOINLINE void solve_B(void *self, float val) { static_cast<ObjectB *>(self)->value += math_core(val, 20, 1.2f, 0.4f, 0.8f, 0.3f); }
NOINLINE void solve_C(void *self, float val) { static_cast<ObjectC *>(self)->value += math_core(val, 25, 1.3f, 0.3f, 0.7f, 0.4f); }
NOINLINE void solve_D(void *self, float val) { static_cast<ObjectD *>(self)->value += math_core(val, 18, 1.4f, 0.2f, 0.6f, 0.5f); }
NOINLINE void solve_E(void *self, float val) { static_cast<ObjectE *>(self)->value += math_core(val, 22, 1.5f, 0.1f, 0.5f, 0.6f); }
NOINLINE void solve_F(void *self, float val) { static_cast<ObjectF *>(self)->value += math_core(val, 24, 1.6f, 0.6f, 0.4f, 0.7f); }
NOINLINE void solve_G(void *self, float val) { static_cast<ObjectG *>(self)->value += math_core(val, 17, 1.7f, 0.7f, 0.3f, 0.8f); }
NOINLINE void solve_H(void *self, float val) { static_cast<ObjectH *>(self)->value += math_core(val, 21, 1.8f, 0.8f, 0.2f, 0.9f); }
NOINLINE void solve_I(void *self, float val) { static_cast<ObjectI *>(self)->value += math_core(val, 23, 1.9f, 0.9f, 0.1f, 1.0f); }
NOINLINE void solve_J(void *self, float val) { static_cast<ObjectJ *>(self)->value += math_core(val, 19, 2.0f, 1.0f, 0.95f, 1.1f); }
NOINLINE void solve_K(void *self, float val) { static_cast<ObjectK *>(self)->value += math_core(val, 26, 2.1f, 1.1f, 0.85f, 1.2f); }
NOINLINE void solve_L(void *self, float val) { static_cast<ObjectL *>(self)->value += math_core(val, 28, 2.2f, 1.2f, 0.75f, 1.3f); }

static const ISolverVTable VTableA{.solve = solve_A};
static const ISolverVTable VTableB{.solve = solve_B};
static const ISolverVTable VTableC{.solve = solve_C};
static const ISolverVTable VTableD{.solve = solve_D};
static const ISolverVTable VTableE{.solve = solve_E};
static const ISolverVTable VTableF{.solve = solve_F};
static const ISolverVTable VTableG{.solve = solve_G};
static const ISolverVTable VTableH{.solve = solve_H};
static const ISolverVTable VTableI{.solve = solve_I};
static const ISolverVTable VTableJ{.solve = solve_J};
static const ISolverVTable VTableK{.solve = solve_K};
static const ISolverVTable VTableL{.solve = solve_L};

// =========================================================================
// 🚀 方案二：去虚化大 switch-case 类型派发
// =========================================================================
NOINLINE void direct_solve_A(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 15, 1.1f, 0.5f, 0.9f, 0.2f); }
NOINLINE void direct_solve_B(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 20, 1.2f, 0.4f, 0.8f, 0.3f); }
NOINLINE void direct_solve_C(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 25, 1.3f, 0.3f, 0.7f, 0.4f); }
NOINLINE void direct_solve_D(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 18, 1.4f, 0.2f, 0.6f, 0.5f); }
NOINLINE void direct_solve_E(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 22, 1.5f, 0.1f, 0.5f, 0.6f); }
NOINLINE void direct_solve_F(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 24, 1.6f, 0.6f, 0.4f, 0.7f); }
NOINLINE void direct_solve_G(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 17, 1.7f, 0.7f, 0.3f, 0.8f); }
NOINLINE void direct_solve_H(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 21, 1.8f, 0.8f, 0.2f, 0.9f); }
NOINLINE void direct_solve_I(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 23, 1.9f, 0.9f, 0.1f, 1.0f); }
NOINLINE void direct_solve_J(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 19, 2.0f, 1.0f, 0.95f, 1.1f); }
NOINLINE void direct_solve_K(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 26, 2.1f, 1.1f, 0.85f, 1.2f); }
NOINLINE void direct_solve_L(BaseObject &obj, float val) noexcept { obj.value += math_core(val, 28, 2.2f, 1.2f, 0.75f, 1.3f); }

// =========================================================================
// 🚀 方案三与四：标准 C++ 虚接口与具体类定义 (含有 vptr)
// =========================================================================
class CppBaseObject {
public:
    ObjectType type;
    float value = 0.0f;
    float init_val = 0.0f;
    virtual ~CppBaseObject() = default;
    virtual void solve(float val) noexcept = 0;
};

#define DEFINE_CPP_CLASS(suffix, loops, c1, c2, c3, c4) \
class CppObject##suffix : public CppBaseObject { \
public: \
    NOINLINE void solve(float val) noexcept override { \
        value += math_core(val, loops, c1, c2, c3, c4); \
    } \
};

DEFINE_CPP_CLASS(A, 15, 1.1f, 0.5f, 0.9f, 0.2f)
DEFINE_CPP_CLASS(B, 20, 1.2f, 0.4f, 0.8f, 0.3f)
DEFINE_CPP_CLASS(C, 25, 1.3f, 0.3f, 0.7f, 0.4f)
DEFINE_CPP_CLASS(D, 18, 1.4f, 0.2f, 0.6f, 0.5f)
DEFINE_CPP_CLASS(E, 22, 1.5f, 0.1f, 0.5f, 0.6f)
DEFINE_CPP_CLASS(F, 24, 1.6f, 0.6f, 0.4f, 0.7f)
DEFINE_CPP_CLASS(G, 17, 1.7f, 0.7f, 0.3f, 0.8f)
DEFINE_CPP_CLASS(H, 21, 1.8f, 0.8f, 0.2f, 0.9f)
DEFINE_CPP_CLASS(I, 23, 1.9f, 0.9f, 0.1f, 1.0f)
DEFINE_CPP_CLASS(J, 19, 2.0f, 1.0f, 0.95f, 1.1f)
DEFINE_CPP_CLASS(K, 26, 2.1f, 1.1f, 0.85f, 1.2f)
DEFINE_CPP_CLASS(L, 28, 2.2f, 1.2f, 0.75f, 1.3f)

constexpr size_t CPP_OBJ_SIZE = sizeof(CppObjectA);
constexpr size_t CPP_OBJ_ALIGN = alignof(CppObjectA);
struct alignas(64) AlignedStorage {
    char data[CPP_OBJ_SIZE];
};

// =========================================================================
// 🚀 方案五：真·无多态扁平普通结构体（无任何继承，无 vptr，仅占 8 字节）
// =========================================================================
#define DEFINE_PLAIN_STRUCT_AND_FUNC(suffix, loops, c1, c2, c3, c4) \
struct Plain##suffix { \
    float value = 0.0f; \
    float init_val = 0.0f; \
}; \
inline void update_##suffix(Plain##suffix& obj, float val) noexcept { \
    obj.value += math_core(val, loops, c1, c2, c3, c4); \
}

DEFINE_PLAIN_STRUCT_AND_FUNC(A, 15, 1.1f, 0.5f, 0.9f, 0.2f)
DEFINE_PLAIN_STRUCT_AND_FUNC(B, 20, 1.2f, 0.4f, 0.8f, 0.3f)
DEFINE_PLAIN_STRUCT_AND_FUNC(C, 25, 1.3f, 0.3f, 0.7f, 0.4f)
DEFINE_PLAIN_STRUCT_AND_FUNC(D, 18, 1.4f, 0.2f, 0.6f, 0.5f)
DEFINE_PLAIN_STRUCT_AND_FUNC(E, 22, 1.5f, 0.1f, 0.5f, 0.6f)
DEFINE_PLAIN_STRUCT_AND_FUNC(F, 24, 1.6f, 0.6f, 0.4f, 0.7f)
DEFINE_PLAIN_STRUCT_AND_FUNC(G, 17, 1.7f, 0.7f, 0.3f, 0.8f)
DEFINE_PLAIN_STRUCT_AND_FUNC(H, 21, 1.8f, 0.8f, 0.2f, 0.9f)
DEFINE_PLAIN_STRUCT_AND_FUNC(I, 23, 1.9f, 0.9f, 0.1f, 1.0f)
DEFINE_PLAIN_STRUCT_AND_FUNC(J, 19, 2.0f, 1.0f, 0.95f, 1.1f)
DEFINE_PLAIN_STRUCT_AND_FUNC(K, 26, 2.1f, 1.1f, 0.85f, 1.2f)
DEFINE_PLAIN_STRUCT_AND_FUNC(L, 28, 2.2f, 1.2f, 0.75f, 1.3f)

// =========================================================================
// 💡 测试主程序
// =========================================================================
int main() {
  std::vector<BaseObject> objects(OBJECT_COUNT);
  std::vector<AlignedStorage> cpp_objects_buffer(OBJECT_COUNT);
  std::vector<CppBaseObject*> cpp_objects(OBJECT_COUNT);

  // 方案四：12 个含有多态包袱（vptr）的去虚化容器
  std::vector<CppObjectA> cpp_objects_A;
  std::vector<CppObjectB> cpp_objects_B;
  std::vector<CppObjectC> cpp_objects_C;
  std::vector<CppObjectD> cpp_objects_D;
  std::vector<CppObjectE> cpp_objects_E;
  std::vector<CppObjectF> cpp_objects_F;
  std::vector<CppObjectG> cpp_objects_G;
  std::vector<CppObjectH> cpp_objects_H;
  std::vector<CppObjectI> cpp_objects_I;
  std::vector<CppObjectJ> cpp_objects_J;
  std::vector<CppObjectK> cpp_objects_K;
  std::vector<CppObjectL> cpp_objects_L;

  // 🚀 方案五：12 个完全不含多态、没有继承、没有 vptr 的纯 POD 扁平容器（每个对象仅占 8 字节）
  std::vector<PlainA> plain_A;
  std::vector<PlainB> plain_B;
  std::vector<PlainC> plain_C;
  std::vector<PlainD> plain_D;
  std::vector<PlainE> plain_E;
  std::vector<PlainF> plain_F;
  std::vector<PlainG> plain_G;
  std::vector<PlainH> plain_H;
  std::vector<PlainI> plain_I;
  std::vector<PlainJ> plain_J;
  std::vector<PlainK> plain_K;
  std::vector<PlainL> plain_L;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(0, 11);

  for (size_t i = 0; i < OBJECT_COUNT; ++i) {
    int r = distrib(gen);
    auto type = static_cast<ObjectType>(r);
    float initial_val = static_cast<float>(i % 100);
    
    // 初始化方案一与二
    objects[i].type = type;
    objects[i].value = initial_val;
    objects[i].init_val = initial_val;
    switch (type) {
    case ObjectType::TypeA: objects[i].vptr = &VTableA; break;
    case ObjectType::TypeB: objects[i].vptr = &VTableB; break;
    case ObjectType::TypeC: objects[i].vptr = &VTableC; break;
    case ObjectType::TypeD: objects[i].vptr = &VTableD; break;
    case ObjectType::TypeE: objects[i].vptr = &VTableE; break;
    case ObjectType::TypeF: objects[i].vptr = &VTableF; break;
    case ObjectType::TypeG: objects[i].vptr = &VTableG; break;
    case ObjectType::TypeH: objects[i].vptr = &VTableH; break;
    case ObjectType::TypeI: objects[i].vptr = &VTableI; break;
    case ObjectType::TypeJ: objects[i].vptr = &VTableJ; break;
    case ObjectType::TypeK: objects[i].vptr = &VTableK; break;
    case ObjectType::TypeL: objects[i].vptr = &VTableL; break;
    }

    // 初始化方案三（标准 C++ 随机连续多态）
    void* ptr = &cpp_objects_buffer[i];
    CppBaseObject* cpp_obj = nullptr;
    switch (type) {
    case ObjectType::TypeA: cpp_obj = new (ptr) CppObjectA(); break;
    case ObjectType::TypeB: cpp_obj = new (ptr) CppObjectB(); break;
    case ObjectType::TypeC: cpp_obj = new (ptr) CppObjectC(); break;
    case ObjectType::TypeD: cpp_obj = new (ptr) CppObjectD(); break;
    case ObjectType::TypeE: cpp_obj = new (ptr) CppObjectE(); break;
    case ObjectType::TypeF: cpp_obj = new (ptr) CppObjectF(); break;
    case ObjectType::TypeG: cpp_obj = new (ptr) CppObjectG(); break;
    case ObjectType::TypeH: cpp_obj = new (ptr) CppObjectH(); break;
    case ObjectType::TypeI: cpp_obj = new (ptr) CppObjectI(); break;
    case ObjectType::TypeJ: cpp_obj = new (ptr) CppObjectJ(); break;
    case ObjectType::TypeK: cpp_obj = new (ptr) CppObjectK(); break;
    case ObjectType::TypeL: cpp_obj = new (ptr) CppObjectL(); break;
    }
    cpp_obj->type = type;
    cpp_obj->value = initial_val;
    cpp_obj->init_val = initial_val;
    cpp_objects[i] = cpp_obj;

    // 初始化方案四
    switch (type) {
    case ObjectType::TypeA: { CppObjectA o; o.value = initial_val; o.init_val = initial_val; cpp_objects_A.push_back(o); break; }
    case ObjectType::TypeB: { CppObjectB o; o.value = initial_val; o.init_val = initial_val; cpp_objects_B.push_back(o); break; }
    case ObjectType::TypeC: { CppObjectC o; o.value = initial_val; o.init_val = initial_val; cpp_objects_C.push_back(o); break; }
    case ObjectType::TypeD: { CppObjectD o; o.value = initial_val; o.init_val = initial_val; cpp_objects_D.push_back(o); break; }
    case ObjectType::TypeE: { CppObjectE o; o.value = initial_val; o.init_val = initial_val; cpp_objects_E.push_back(o); break; }
    case ObjectType::TypeF: { CppObjectF o; o.value = initial_val; o.init_val = initial_val; cpp_objects_F.push_back(o); break; }
    case ObjectType::TypeG: { CppObjectG o; o.value = initial_val; o.init_val = initial_val; cpp_objects_G.push_back(o); break; }
    case ObjectType::TypeH: { CppObjectH o; o.value = initial_val; o.init_val = initial_val; cpp_objects_H.push_back(o); break; }
    case ObjectType::TypeI: { CppObjectI o; o.value = initial_val; o.init_val = initial_val; cpp_objects_I.push_back(o); break; }
    case ObjectType::TypeJ: { CppObjectJ o; o.value = initial_val; o.init_val = initial_val; cpp_objects_J.push_back(o); break; }
    case ObjectType::TypeK: { CppObjectK o; o.value = initial_val; o.init_val = initial_val; cpp_objects_K.push_back(o); break; }
    case ObjectType::TypeL: { CppObjectL o; o.value = initial_val; o.init_val = initial_val; cpp_objects_L.push_back(o); break; }
    }

    // 初始化方案五（真·无多态结构体）
    switch (type) {
    case ObjectType::TypeA: { PlainA o; o.value = initial_val; o.init_val = initial_val; plain_A.push_back(o); break; }
    case ObjectType::TypeB: { PlainB o; o.value = initial_val; o.init_val = initial_val; plain_B.push_back(o); break; }
    case ObjectType::TypeC: { PlainC o; o.value = initial_val; o.init_val = initial_val; plain_C.push_back(o); break; }
    case ObjectType::TypeD: { PlainD o; o.value = initial_val; o.init_val = initial_val; plain_D.push_back(o); break; }
    case ObjectType::TypeE: { PlainE o; o.value = initial_val; o.init_val = initial_val; plain_E.push_back(o); break; }
    case ObjectType::TypeF: { PlainF o; o.value = initial_val; o.init_val = initial_val; plain_F.push_back(o); break; }
    case ObjectType::TypeG: { PlainG o; o.value = initial_val; o.init_val = initial_val; plain_G.push_back(o); break; }
    case ObjectType::TypeH: { PlainH o; o.value = initial_val; o.init_val = initial_val; plain_H.push_back(o); break; }
    case ObjectType::TypeI: { PlainI o; o.value = initial_val; o.init_val = initial_val; plain_I.push_back(o); break; }
    case ObjectType::TypeJ: { PlainJ o; o.value = initial_val; o.init_val = initial_val; plain_J.push_back(o); break; }
    case ObjectType::TypeK: { PlainK o; o.value = initial_val; o.init_val = initial_val; plain_K.push_back(o); break; }
    case ObjectType::TypeL: { PlainL o; o.value = initial_val; o.init_val = initial_val; plain_L.push_back(o); break; }
    }
  }

  std::cout << "【允许内联提示模式下】五路数据排列对齐准备完毕。\n"
            << "异构对象总数量: " << OBJECT_COUNT << "，开始测试...\n\n";

  // 数据重置 Lambda
  auto reset_all_data = [&]() {
      for (size_t i = 0; i < OBJECT_COUNT; ++i) {
          objects[i].value = objects[i].init_val;
          cpp_objects[i]->value = cpp_objects[i]->init_val;
      }
      for (auto& o : cpp_objects_A) o.value = o.init_val;
      for (auto& o : cpp_objects_B) o.value = o.init_val;
      for (auto& o : cpp_objects_C) o.value = o.init_val;
      for (auto& o : cpp_objects_D) o.value = o.init_val;
      for (auto& o : cpp_objects_E) o.value = o.init_val;
      for (auto& o : cpp_objects_F) o.value = o.init_val;
      for (auto& o : cpp_objects_G) o.value = o.init_val;
      for (auto& o : cpp_objects_H) o.value = o.init_val;
      for (auto& o : cpp_objects_I) o.value = o.init_val;
      for (auto& o : cpp_objects_J) o.value = o.init_val;
      for (auto& o : cpp_objects_K) o.value = o.init_val;
      for (auto& o : cpp_objects_L) o.value = o.init_val;

      for (auto& o : plain_A) o.value = o.init_val;
      for (auto& o : plain_B) o.value = o.init_val;
      for (auto& o : plain_C) o.value = o.init_val;
      for (auto& o : plain_D) o.value = o.init_val;
      for (auto& o : plain_E) o.value = o.init_val;
      for (auto& o : plain_F) o.value = o.init_val;
      for (auto& o : plain_G) o.value = o.init_val;
      for (auto& o : plain_H) o.value = o.init_val;
      for (auto& o : plain_I) o.value = o.init_val;
      for (auto& o : plain_J) o.value = o.init_val;
      for (auto& o : plain_K) o.value = o.init_val;
      for (auto& o : plain_L) o.value = o.init_val;
  };

  // =========================================================================
  // 💡 方案一：C 风格虚表 (VTable) 间接调用
  // =========================================================================
  reset_all_data();
  double vtable_total_time = 0.0;
  {
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        BaseObject &obj = objects[i];
        obj.vptr->solve(&obj, 1.0f);
      }
      auto end = std::chrono::high_resolution_clock::now();
      vtable_total_time += std::chrono::duration<double, std::milli>(end - start).count();
    }
  }

  // =========================================================================
  // 💡 方案二：标准 C++ 虚函数 (Cpp-VTable) 间接调用
  // =========================================================================
  reset_all_data();
  double cpp_vtable_total_time = 0.0;
  {
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        cpp_objects[i]->solve(1.0f);
      }
      auto end = std::chrono::high_resolution_clock::now();
      cpp_vtable_total_time += std::chrono::duration<double, std::milli>(end - start).count();
    }
  }

  // =========================================================================
  // 💡 方案三：大 switch-case 类型派发
  // =========================================================================
  reset_all_data();
  double switch_total_time = 0.0;
  {
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < OBJECT_COUNT; ++i) {
        BaseObject &obj = objects[i];
        switch (obj.type) {
        case ObjectType::TypeA: direct_solve_A(obj, 1.0f); break;
        case ObjectType::TypeB: direct_solve_B(obj, 1.0f); break;
        case ObjectType::TypeC: direct_solve_C(obj, 1.0f); break;
        case ObjectType::TypeD: direct_solve_D(obj, 1.0f); break;
        case ObjectType::TypeE: direct_solve_E(obj, 1.0f); break;
        case ObjectType::TypeF: direct_solve_F(obj, 1.0f); break;
        case ObjectType::TypeG: direct_solve_G(obj, 1.0f); break;
        case ObjectType::TypeH: direct_solve_H(obj, 1.0f); break;
        case ObjectType::TypeI: direct_solve_I(obj, 1.0f); break;
        case ObjectType::TypeJ: direct_solve_J(obj, 1.0f); break;
        case ObjectType::TypeK: direct_solve_K(obj, 1.0f); break;
        case ObjectType::TypeL: direct_solve_L(obj, 1.0f); break;
        }
      }
      auto end = std::chrono::high_resolution_clock::now();
      switch_total_time += std::chrono::duration<double, std::milli>(end - start).count();
    }
  }

  // =========================================================================
  // 🚀 方案四：类型隔离存储（100% 编译期静态去虚化 + 强力内联）
  // =========================================================================
  reset_all_data();
  double devirt_total_time = 0.0;
  {
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();

      for (auto& o : cpp_objects_A) o.solve(1.0f);
      for (auto& o : cpp_objects_B) o.solve(1.0f);
      for (auto& o : cpp_objects_C) o.solve(1.0f);
      for (auto& o : cpp_objects_D) o.solve(1.0f);
      for (auto& o : cpp_objects_E) o.solve(1.0f);
      for (auto& o : cpp_objects_F) o.solve(1.0f);
      for (auto& o : cpp_objects_G) o.solve(1.0f);
      for (auto& o : cpp_objects_H) o.solve(1.0f);
      for (auto& o : cpp_objects_I) o.solve(1.0f);
      for (auto& o : cpp_objects_J) o.solve(1.0f);
      for (auto& o : cpp_objects_K) o.solve(1.0f);
      for (auto& o : cpp_objects_L) o.solve(1.0f);

      auto end = std::chrono::high_resolution_clock::now();
      devirt_total_time += std::chrono::duration<double, std::milli>(end - start).count();
    }
  }

  // =========================================================================
  // 🚀 方案五：真·无多态扁平普通结构体（0 虚表，0 继承，0 动态，极简 8 字节）
  // =========================================================================
  reset_all_data();
  double plain_total_time = 0.0;
  {
    for (size_t iter = 0; iter < ITERATIONS; ++iter) {
      auto start = std::chrono::high_resolution_clock::now();

      // 完全硬编码，直接调用无多态 C 风格普通函数
      for (auto& o : plain_A) update_A(o, 1.0f);
      for (auto& o : plain_B) update_B(o, 1.0f);
      for (auto& o : plain_C) update_C(o, 1.0f);
      for (auto& o : plain_D) update_D(o, 1.0f);
      for (auto& o : plain_E) update_E(o, 1.0f);
      for (auto& o : plain_F) update_F(o, 1.0f);
      for (auto& o : plain_G) update_G(o, 1.0f);
      for (auto& o : plain_H) update_H(o, 1.0f);
      for (auto& o : plain_I) update_I(o, 1.0f);
      for (auto& o : plain_J) update_J(o, 1.0f);
      for (auto& o : plain_K) update_K(o, 1.0f);
      for (auto& o : plain_L) update_L(o, 1.0f);

      auto end = std::chrono::high_resolution_clock::now();
      plain_total_time += std::chrono::duration<double, std::milli>(end - start).count();
    }
  }

  // 校验和计算，确保计算对齐
  double checksum_v = 0.0, checksum_cpp = 0.0, checksum_devirt = 0.0, checksum_plain = 0.0;
  for (size_t i = 0; i < OBJECT_COUNT; ++i) {
    checksum_v += objects[i].value;
    checksum_cpp += cpp_objects[i]->value;
  }
  auto sum_devirt = [&](const auto& vec) { for (const auto& o : vec) checksum_devirt += o.value; };
  sum_devirt(cpp_objects_A); sum_devirt(cpp_objects_B); sum_devirt(cpp_objects_C);
  sum_devirt(cpp_objects_D); sum_devirt(cpp_objects_E); sum_devirt(cpp_objects_F);
  sum_devirt(cpp_objects_G); sum_devirt(cpp_objects_H); sum_devirt(cpp_objects_I);
  sum_devirt(cpp_objects_J); sum_devirt(cpp_objects_K); sum_devirt(cpp_objects_L);

  auto sum_plain = [&](const auto& vec) { for (const auto& o : vec) checksum_plain += o.value; };
  sum_plain(plain_A); sum_plain(plain_B); sum_plain(plain_C);
  sum_plain(plain_D); sum_plain(plain_E); sum_plain(plain_F);
  sum_plain(plain_G); sum_plain(plain_H); sum_plain(plain_I);
  sum_plain(plain_J); sum_plain(plain_K); sum_plain(plain_L);

  for (size_t i = 0; i < OBJECT_COUNT; ++i) {
      cpp_objects[i]->~CppBaseObject();
  }

  std::cout << "----------------- 测试结果 -----------------\n";
  std::cout << "C风格虚表 (C-VTable) 总平均耗时:         " << vtable_total_time / ITERATIONS << " ms\n";
  std::cout << "标准 C++ 虚表 (Cpp-VTable) 平均耗时:      " << cpp_vtable_total_time / ITERATIONS << " ms\n";
  std::cout << "大 Switch-Case 跳转表平均耗时:            " << switch_total_time / ITERATIONS << " ms\n";
  std::cout << "100% 静态去虚化 (Monomorphic) 平均耗时:   " << devirt_total_time / ITERATIONS << " ms\n";
  std::cout << "真·无多态普通结构体 (Plain) 平均耗时:     " << plain_total_time / ITERATIONS << " ms\n";
  std::cout << "--------------------------------------------\n";
  std::cout << "(校验和 C-VTable: " << checksum_v << ")\n";
  std::cout << "(校验和 Cpp-VTable: " << checksum_cpp << ")\n";
  std::cout << "(校验和 去虚化: " << checksum_devirt << ")\n";
  std::cout << "(校验和 无多态: " << checksum_plain << ")\n";

  return 0;
}
