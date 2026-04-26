#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>
#include "../types/mesh.hpp"
#include "../types/tri_mesh_halfedge_3d.hpp"
#include "../utils/block_deque.hpp"
#include "../utils/flat_map.hpp"
#include <set>
#include <unordered_set>
#include <utility>
#include "plotter.hpp"
#include "solver.hpp"
#include "../utils/parallel_for.hpp"
#include "utils/platonic_data.hpp"

namespace StuCanvas
{
    template <typename T>
    struct Graph;
    template <typename T>
    struct Node;
    template <typename T>
    using SolverFuncPtr = void (*)(Graph<T>&, Node<T>&);
    template <typename T>
    using PlotterFuncPtr = void (*)(Graph<T>&, Node<T>&);


    enum class NodeMask : uint64_t
    {
        NONE = 0,
        DIRTY = 1ULL << 0,
        VISITED = 1ULL << 1,
        HIDDEN = 1ULL << 2,
        FROZEN = 1ULL << 3,
        SELECTED = 1ULL << 4
    };

    enum class NodeType : uint32_t
    {
        MASK_WORLD = 0xFF000000,
        MASK_CATEGORY = 0x00FF0000,
        MASK_SPECIFIC = 0x0000FFFF,


        WORLD_2D = 0x01000000,
        WORLD_3D = 0x02000000,
        WORLD_META = 0x04000000,


        CAT_POINT = 0x00010000,
        CAT_LINEAR = 0x00020000,
        CAT_CURVE = 0x00030000,
        CAT_SURFACE = 0x00040000,
        CAT_FUNCTION = 0x00050000,
        CAT_SCALAR = 0x00060000,

        POINT_2D_FREE = WORLD_2D | CAT_POINT | 0x0001,
        POINT_2D_INTERSECT = WORLD_2D | CAT_POINT | 0x0002,
        POINT_2D_MID = WORLD_2D | CAT_POINT | 0x0003,
        POINT_2D_SNAP = WORLD_2D | CAT_POINT | 0x0004,

        LINE_2D_SEGMENT = WORLD_2D | CAT_LINEAR | 0x0001,
        LINE_2D_STRAIGHT = WORLD_2D | CAT_LINEAR | 0x0002,
        LINE_2D_TANGENT = WORLD_2D | CAT_LINEAR | 0x0003,
        LINE_2D_RAY = WORLD_2D | CAT_LINEAR | 0x0004,
        POLY_2D_GENERATIVE = WORLD_2D | CAT_CURVE | 0x0005,

        CIRCLE_2D = WORLD_2D | CAT_CURVE | 0x0001,
        ARC_2D = WORLD_2D | CAT_CURVE | 0x0002,
        RECT_2D = WORLD_2D | CAT_CURVE | 0x0006, // 新增：2D 矩形


        POINT_3D_FREE = WORLD_3D | CAT_POINT | 0x0001,
        POINT_3D_INTERSECT = WORLD_3D | CAT_POINT | 0x0002,
        POINT_3D_MID = WORLD_3D | CAT_POINT | 0x0003,
        POINT_3D_SNAP = WORLD_3D | CAT_POINT | 0x0004,

        LINE_3D_STRAIGHT = WORLD_3D | CAT_LINEAR | 0x0001,
        LINE_3D_SEGMENT = WORLD_3D | CAT_LINEAR | 0x0002,
        LINE_3D_TANGENT = WORLD_2D | CAT_LINEAR | 0x0003,
        LINE_3D_RAY = WORLD_2D | CAT_LINEAR | 0x0004,


        PLANE_3D = WORLD_3D | CAT_SURFACE | 0x0001,
        SPHERE_3D = WORLD_3D | CAT_SURFACE | 0x0002,
        CYLINDER_3D = WORLD_3D | CAT_SURFACE | 0x0003,
        PLANE_TANGENT_3D = WORLD_3D | CAT_SURFACE | 0x0004,
        PLANE_3D_TRAINGLE = WORLD_3D | CAT_SURFACE | 0x0005,
        CURVE_3D_INTERSECTION = WORLD_3D | CAT_CURVE | 0x0001,
        CUBOID_3D = WORLD_3D | CAT_SURFACE | 0x0006, // 新增：3D 长方体
        CONE_3D = WORLD_3D | CAT_SURFACE | 0x0007,
        CIRCLE_3D = WORLD_3D | CAT_CURVE | 0x0002,


        SCALAR = WORLD_META | CAT_SCALAR | 0x0001,
        FUNC_EXPLICIT = WORLD_META | CAT_FUNCTION | 0x0001,
        FUNC_PARAMETRIC = WORLD_META | CAT_FUNCTION | 0x0002,

        BLACKBOX_2D = WORLD_2D | CAT_FUNCTION | 0x0003, // 2D 自定义黑盒
        BLACKBOX_3D = WORLD_3D | CAT_FUNCTION | 0x0003, // 3D 自定义黑盒

        UNKNOWN = 0x00000000
    };


    [[nodiscard]] constexpr bool is_2d(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0xFF000000) == 0x01000000;
    }


    [[nodiscard]] constexpr bool is_3d(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0xFF000000) == 0x02000000;
    }


    [[nodiscard]] constexpr bool is_point(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0x00FF0000) == 0x00010000;
    }


    [[nodiscard]] constexpr bool is_linear(NodeType t) noexcept
    {
        return (std::to_underlying(t) & 0x00FF0000) == 0x00020000;
    }

    template <typename T>
    struct Selection
    {
        Graph<T>& graph;
        std::vector<uint64_t> ids;

        // --- 链式筛选方法 ---
        Selection& ByName(const std::string& name)
        {
            // 使用 std::erase_if (C++20) 或 传统的 remove_if 逻辑进行原地过滤，效率更高
            std::erase_if(ids, [&](uint64_t id)
            {
                return graph.GetNode(id)->name != name;
            });
            return *this;
        }

        Selection& ByType(NodeType type)
        {
            std::erase_if(ids, [&](uint64_t id)
            {
                return graph.GetNode(id)->type != type;
            });
            return *this;
        }

        using NodePredicate = bool (*)(NodeType);

        Selection& ByPredicate(NodePredicate pred)
        {
            std::erase_if(ids, [&](uint64_t id)
            {
                return !pred(graph.GetNode(id)->type);
            });
            return *this;
        }

        // --- 核心魔法：隐式转换 ---
        // 当这个对象被赋值给 std::vector<uint64_t> 时自动调用
        operator std::vector<uint64_t>() &&
        {
            return std::move(ids);
        }

        // 允许在没有隐式转换的情况下也能直接拷贝
        operator std::vector<uint64_t>() const &
        {
            return ids;
        }

        // --- 核心魔法：支持 range-based for 循环 ---
        auto begin() { return ids.begin(); }
        auto end() { return ids.end(); }
        auto begin() const { return ids.begin(); }
        auto end() const { return ids.end(); }
    };



    template <typename T>
    struct Node
    {
        using value_type = T;
        uint64_t id{};
        std::string name = "Unnamed";
        static inline std::atomic<uint64_t> global_node_id_counter{0};
        NodeType type = NodeType::UNKNOWN;
        uint64_t mask = 0;
        void set_mask(NodeMask m) noexcept { mask |= static_cast<uint64_t>(m); }

        void clear_mask(NodeMask m) noexcept { mask &= ~static_cast<uint64_t>(m); }

        [[nodiscard]] bool has_mask(NodeMask m) const noexcept { return (mask & static_cast<uint64_t>(m)) != 0; }

        union
        {
            struct
            {
                T x;
                T y;
            } point_2d;

            struct
            {
                T x;
                T y;
                T z;
            } point_3d;

            struct {
                T cx, cy, cz; // 中心点坐标 (由 Solver 更新)
                T dx, dy, dz; // X, Y, Z 三个方向的半延伸长度
            } cuboid_3d;

            struct
            {
                T x0;
                T y0;
                T x1;
                T y1;
            } line_2d;

            struct
            {
                T x0;
                T y0;
                T z0;
                T x1;
                T y1;
                T z1;
            } line_3d;


            struct {
                T apex_x, apex_y, apex_z; // 顶点 (由 Solver 更新)
                T base_x, base_y, base_z; // 底面圆心 (由 Solver 更新)
                T r;                      // 底面半径
            } cone_3d;

            struct
            {
                T cx;
                T cy;
                T r;
            } circle_2d;


            struct
            {
                T cx;
                T cy;
                T cz;
                T nx;
                T ny;
                T nz;
                T r;
            } circle_3d;
            struct {
                T cx, cy;      // 中心点 (由 Solver 更新)
                T width, height; // 宽和高
            } rect_2d;         // 新增：矩形数据

            struct {
                T cx, cy, cz; // 中心点 (由 Solver 更新)
                T r;          // 半径
            } sphere_3d;

            struct
            {
                T ox;
                T oy;
                T oz;
                T ux;
                T uy;
                T uz;
                T vx;
                T vy;
                T vz;
            } plane_3d;

            struct
            {
                T cx, cy, r;
                T start_angle;
                T end_angle;
            } arc_2d;


            struct {
                T p1x, p1y, p1z; // 起点中心
                T p2x, p2y, p2z; // 终点中心
                T r;             // 半径
            } cylinder_3d;


            struct {
                T vals[16];
            } blackbox;

            struct
            {
                T x0, y0, x1, y1;
                T distance;
            } parallel_line_2d;

            struct
            {
                T x, y;
                T guess_x, guess_y;
            } snap_2d;

            struct
            {
                T x, y, z;
                T guess_x, guess_y, guess_z;
            } snap_3d;

            struct
            {
                T cx, cy;
                T side_len;
                uint32_t n;
            } poly_gen_2d;

            struct {
                T cx, cy, cz; // 中心点 (由 Solver 更新)
                T radius;     // 半径
                int type;     // 4, 6, 8, 12, 20
            } platonic_solid_3d;
        } data;

        utils::BlockDeque<uint64_t, 4> parents;
        utils::BlockDeque<uint64_t, 16> children;
        std::vector<Point2D<T>> result_points_2d;
        std::vector<Point3D<T>> result_points_3d;
        SolverFuncPtr<T> solver = nullptr;
        PlotterFuncPtr<T> plotter = nullptr;

        Node() noexcept
        {
            id = global_node_id_counter.fetch_add(1, std::memory_order_relaxed);
        }




    };

    template <typename T>
    struct Graph
    {
        using value_type = T;
        utils::BlockDeque<Node<T>, 256> node_pool;
        std::string name;
        utils::FlatMap<uint64_t, size_t> id_map;

        Node<T>* GetNode(uint64_t id)
        {
            auto it = id_map.find(id);
            if (it == id_map.end()) throw std::out_of_range("Invalid node id");
            return &node_pool[it->second];
        }

        // 在 Graph 结构体内
        Selection<T> Select()
        {
            std::vector<uint64_t> all_ids;
            all_ids.reserve(node_pool.size());
            for (size_t i = 0; i < node_pool.size(); ++i) all_ids.push_back(node_pool[i].id);
            return {*this, std::move(all_ids)};
        }

        Selection<T> Select(const std::vector<uint64_t>& scope)
        {
            return {*this, scope};
        }

        void ClearMaskPool(NodeMask m)
        {
            // 线性遍历整个 BlockDeque 内存池
            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                node_pool[i].clear_mask(m);
            }
        }

        std::vector<std::vector<uint64_t>> CompileLeveledOrder()
        {
            std::vector<std::vector<uint64_t>> levels;
            if (node_pool.empty()) return levels;


            utils::FlatMap<uint64_t, size_t> in_degrees;
            std::vector<uint64_t> current_level_nodes;

            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                auto& node = node_pool[i];
                size_t d = node.parents.size();
                in_degrees[node.id] = d;
                if (d == 0)
                {
                    current_level_nodes.emplace_back(node.id);
                }
            }


            size_t processed_count = 0;
            while (!current_level_nodes.empty())
            {
                levels.emplace_back(current_level_nodes);
                processed_count += current_level_nodes.size();

                std::vector<uint64_t> next_level_nodes;
                for (uint64_t curr_id : current_level_nodes)
                {
                    Node<T>* curr_node = GetNode(curr_id);
                    for (uint64_t child_id : curr_node->children)
                    {
                        if (--in_degrees[child_id] == 0)
                        {
                            next_level_nodes.emplace_back(child_id);
                        }
                    }
                }
                current_level_nodes = std::move(next_level_nodes);
            }


            if (processed_count != node_pool.size())
            {
                throw std::runtime_error("Dependency cycle detected in Graph!");
            }

            return levels;
        }


        void PropagateMaskDownstream(uint64_t start_node_id, NodeMask m)
        {
            // 使用 vector 模拟栈 (Stack) 进行深度优先遍历
            std::vector<uint64_t> stack;
            stack.push_back(start_node_id);

            while (!stack.empty())
            {
                uint64_t current_id = stack.back();
                stack.pop_back();

                Node<T>* node = GetNode(current_id);

                // 统一判定逻辑：
                // 如果当前节点已经设置了该掩码，说明该分支已经传播过，直接跳过。
                // 这既保证了效率（O(V+E)），也防止了在复杂 DAG 中的重复计算。
                if (node->has_mask(m)) continue;

                // 设置掩码
                node->set_mask(m);

                // 将所有子节点压入栈中以待后续处理
                // 注意：由于是栈，子节点处理顺序与 children 列表相反，但对结果无影响
                for (uint64_t child_id : node->children)
                {
                    stack.push_back(child_id);
                }
            }
        }

        bool DeleteNode(uint64_t id)
        {
            auto it = id_map.find(id);
            if (it == id_map.end()) return false;

            size_t index_to_delete = it->second;
            uint64_t last_node_id = node_pool.back().id;


            if (index_to_delete != node_pool.size() - 1)
            {
                std::swap(node_pool[index_to_delete], node_pool.back());

                id_map[last_node_id] = index_to_delete;
            }


            node_pool.pop_back();
            id_map.erase(id);
            return true;
        }


        void Compute(uint64_t num_threads = 1)
        {
            // 1. 脏标记向下传播 (必须先于拓扑执行)
            for (size_t i = 0; i < node_pool.size(); ++i)
            {
                if (node_pool[i].has_mask(NodeMask::DIRTY))
                {
                    PropagateMaskDownstream(node_pool[i].id, NodeMask::DIRTY);
                }
            }

            // 2. 计算线程数逻辑
            uint32_t target_threads = ResolveThreadCount(num_threads);

            // 3. 获取分层拓扑序列
            auto levels = CompileLeveledOrder();

            // 4. 按层级执行
            for (auto& level_nodes : levels)
            {
                // 只有当目标线程数 > 1 且 当前层级节点数 > 1 时才启动并行
                if (target_threads > 1 && level_nodes.size() > 1)
                {
                    utils::parallel_for(size_t(0), level_nodes.size(), [&](size_t start, size_t end)
                    {
                        for (size_t i = start; i < end; ++i)
                        {
                            ExecuteNodeUpdate(level_nodes[i], false);
                        }
                    }, target_threads);
                }
                else
                {
                    for (uint64_t id : level_nodes)
                    {
                        ExecuteNodeUpdate(id, false);
                    }
                }
            }
        }

        /**
         * @brief 强制全量计算 (忽略 DIRTY 标记)
         * @param num_threads 线程控制逻辑同 Compute
         */
        void ComputeForce(uint64_t num_threads = 1)
        {
            uint32_t target_threads = ResolveThreadCount(num_threads);
            auto levels = CompileLeveledOrder();

            for (auto& level_nodes : levels)
            {
                if (target_threads > 1 && level_nodes.size() > 1)
                {
                    utils::parallel_for(size_t(0), level_nodes.size(), [&](size_t start, size_t end)
                    {
                        for (size_t i = start; i < end; ++i)
                        {
                            ExecuteNodeUpdate(level_nodes[i], true);
                        }
                    }, target_threads);
                }
                else
                {
                    for (uint64_t id : level_nodes)
                    {
                        ExecuteNodeUpdate(id, true);
                    }
                }
            }
        }

        [[nodiscard]] uint32_t ResolveThreadCount(uint64_t req) const
        {
            uint32_t hardware = std::thread::hardware_concurrency();
            if (hardware == 0) hardware = 1; // 容错处理

            if (req == 0 || req > static_cast<uint64_t>(hardware))
            {
                return hardware;
            }
            return static_cast<uint32_t>(req);
        }

        /**
         * @brief 内部执行单个节点的求解和打点
         * @param id 节点ID
         * @param force 是否强制执行
         */
        void ExecuteNodeUpdate(uint64_t id, bool force)
        {
            Node<T>* node = GetNode(id);

            // 如果不是强制模式且节点不是脏的，则跳过
            if (!force && !node->has_mask(NodeMask::DIRTY)) return;
            node->result_points_2d.clear();
            node->result_points_3d.clear();

            // 1. 执行数学求解 (更新 data)
            if (node->solver)
            {
                node->solver(*this, *node);
            }


            // 2. 执行打点渲染 (更新 result_points)
            if (node->plotter)
            {
                node->plotter(*this, *node);
            }


            // 3. 计算完成，清除脏标记
            node->clear_mask(NodeMask::DIRTY);
        }

        struct
        {
            T x_min;
            T y_min;
            T x_max;
            T y_max;
        } world_space_2d;

        struct
        {
            T x_min;
            T y_min;
            T z_min;
            T x_max;
            T y_max;
            T z_max;
        } world_space_3d;

        struct
        {
            T x, y;
        } resolution_2d;

        struct
        {
            T x, y, z;
        } resolution_3d;


        uint64_t CreateFreePoint_2D(T x, T y, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_FREE;
            node.data.point_2d.x = x;
            node.data.point_2d.y = y;
            id_map[node.id] = node_pool.size() - 1;
            node.solver = nullptr;
            node.plotter = PlotPoint_2D;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return node.id;
        }

        uint64_t CreateFreePoint_3D(T x, T y, T z, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_FREE;
            node.data.point_3d.x = x;
            node.data.point_3d.y = y;
            node.data.point_3d.z = z;
            id_map[node.id] = node_pool.size() - 1;
            node.solver = nullptr;
            node.plotter =  PlotPoint_3D;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return node.id;
        }

        uint64_t CreateStraightLine_2D(uint64_t start_point_id, uint64_t end_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_line_id = node.id;
            id_map[new_line_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_line_id);
            end_node->children.emplace_back(new_line_id);


            node.solver = SolveStraightLine_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;


            return new_line_id;
        }

        uint64_t CreateStraightLine_3D(uint64_t start_point_id, uint64_t end_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_STRAIGHT;


            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_line_id = node.id;
            id_map[new_line_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_line_id);
            end_node->children.emplace_back(new_line_id);


            node.solver = SolveStraightLine_3D<T>;
            node.plotter = PlotStraightLine_3D<T>;


            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_line_id;
        }

        uint64_t CreateSegment_2D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_SEGMENT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveSegment_2D<T>;
            node.plotter = PlotSegment_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }

        uint64_t CreateSegment_3D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_SEGMENT;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveSegment_3D<T>;
            node.plotter = PlotSegment_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_seg_id;
        }


        uint64_t CreateRay_2D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_2d(start_node->type) && is_point(start_node->type)) ||
                !(is_2d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 2D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_RAY;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveRay_2D<T>;
            node.plotter = PlotRay_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }

        uint64_t CreateRay_3D(uint64_t start_point_id, uint64_t end_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* start_node = GetNode(start_point_id);
            Node<T>* end_node = GetNode(end_point_id);


            if (!(is_3d(start_node->type) && is_point(start_node->type)) ||
                !(is_3d(end_node->type) && is_point(end_node->type)))
            {
                throw std::invalid_argument("Both start and end arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_3D_RAY;
            node.parents.emplace_back(start_point_id);
            node.parents.emplace_back(end_point_id);

            uint64_t new_seg_id = node.id;
            id_map[new_seg_id] = node_pool.size() - 1;


            start_node->children.emplace_back(new_seg_id);
            end_node->children.emplace_back(new_seg_id);


            node.solver = SolveRay_3D<T>;
            node.plotter = PlotRay_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_seg_id;
        }


        uint64_t CreateCircle_2D(uint64_t center_point_id, T radius, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_point_id);


            if (!(is_2d(center_node->type) && is_point(center_node->type)))
            {
                throw std::invalid_argument("Center argument must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(center_point_id);


            node.data.circle_2d.r = radius;

            uint64_t new_circle_id = node.id;
            id_map[new_circle_id] = node_pool.size() - 1;


            center_node->children.emplace_back(new_circle_id);


            node.solver = SolveCircle_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_circle_id;
        }


        uint64_t CreateCircle_2D(uint64_t center_id, uint64_t point_on_circle_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(center_id);
            Node<T>* n2 = GetNode(point_on_circle_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) || !(is_2d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(center_id);
            node.parents.emplace_back(point_on_circle_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveCircle_CP_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateCircle_2D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) ||
                !(is_2d(n2->type) && is_point(n2->type)) ||
                !(is_2d(n3->type) && is_point(n3->type)))
            {
                throw std::invalid_argument("All three arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_2D;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            node.solver = SolveCircle_3P_2D<T>;
            node.plotter = PlotCircle_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreatePlane_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            auto is_valid_point = [](Node<T>* n)
            {
                return is_3d(n->type) && is_point(n->type);
            };

            if (!is_valid_point(n1) || !is_valid_point(n2) || !is_valid_point(n3))
            {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_3D;


            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_plane_id = node.id;
            id_map[new_plane_id] = node_pool.size() - 1;


            n1->children.emplace_back(new_plane_id);
            n2->children.emplace_back(new_plane_id);
            n3->children.emplace_back(new_plane_id);


            node.solver = SolvePlane_3Points_3D<T>;
            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_plane_id;
        }

        /**
         * @brief 创建 3D 三角形面
         * @param p1_id 顶点1 (原点)
         * @param p2_id 顶点2 (决定 U 向量)
         * @param p3_id 顶点3 (决定 V 向量)
         */
        uint64_t CreateTriangle_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            auto is_valid_point = [](Node<T>* n) {
                return is_3d(n->type) && is_point(n->type);
            };

            if (!is_valid_point(n1) || !is_valid_point(n2) || !is_valid_point(n3)) {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            // 使用你定义的特定类型 PLANE_3D_TRAINGLE
            node.type = NodeType::PLANE_3D_TRAINGLE;
            node.name = node_name;

            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            // 绑定三角形专用求解器与打点器
            node.solver = SolveTriangle_3Points_3D<T>;
            node.plotter = PlotTriangle_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateArc_2D(uint64_t p_start_id, uint64_t p_on_id, uint64_t p_end_id,
                              std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p_start_id);
            Node<T>* n2 = GetNode(p_on_id);
            Node<T>* n3 = GetNode(p_end_id);

            if (!is_2d(n1->type) || !is_2d(n2->type) || !is_2d(n3->type))
            {
                throw std::invalid_argument("All arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::ARC_2D;
            node.parents.emplace_back(p_start_id);
            node.parents.emplace_back(p_on_id);
            node.parents.emplace_back(p_end_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);

            node.solver = SolveArc_3P_2D<T>;
            node.plotter = PlotArc_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateParallelLine_2D(uint64_t ref_line_id, T distance, std::string node_name = "Unnamed")
        {
            Node<T>* ref_node = GetNode(ref_line_id);


            if (!is_2d(ref_node->type) || !is_linear(ref_node->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;
            node.parents.emplace_back(ref_line_id);


            node.data.parallel_line_2d.distance = distance;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            ref_node->children.emplace_back(new_id);

            node.solver = SolveParallelLine_Dist_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateParallelLine_2D(uint64_t ref_line_id, uint64_t target_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* ref_line = GetNode(ref_line_id);
            Node<T>* target_pt = GetNode(target_point_id);


            if (!is_2d(ref_line->type) || !is_linear(ref_line->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }
            if (!is_2d(target_pt->type) || !is_point(target_pt->type))
            {
                throw std::invalid_argument("Target must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;


            node.parents.emplace_back(ref_line_id);
            node.parents.emplace_back(target_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            ref_line->children.emplace_back(new_id);
            target_pt->children.emplace_back(new_id);


            node.solver = SolveParallelLine_Point_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateVerticalLine_2D(uint64_t ref_line_id, uint64_t target_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* ref_line = GetNode(ref_line_id);
            Node<T>* target_pt = GetNode(target_point_id);


            if (!is_2d(ref_line->type) || !is_linear(ref_line->type))
            {
                throw std::invalid_argument("Reference must be a 2D linear object.");
            }
            if (!is_2d(target_pt->type) || !is_point(target_pt->type))
            {
                throw std::invalid_argument("Target must be a 2D Point object.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_STRAIGHT;

            node.parents.emplace_back(ref_line_id);
            node.parents.emplace_back(target_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            ref_line->children.emplace_back(new_id);
            target_pt->children.emplace_back(new_id);


            node.solver = SolveVerticalLine_Point_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_2D(uint64_t p1_id, uint64_t p2_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);

            if (!(is_2d(n1->type) && is_point(n1->type)) ||
                !(is_2d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 2D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_MID;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveMidPoint_2P_2D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_2D(uint64_t segment_id, std::string node_name = "Unnamed")
        {
            Node<T>* seg_node = GetNode(segment_id);


            if (seg_node->type != NodeType::LINE_2D_SEGMENT)
            {
                throw std::invalid_argument("Argument must be a 2D Segment object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_MID;
            node.parents.emplace_back(segment_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            seg_node->children.emplace_back(new_id);

            node.solver = SolveMidPoint_Segment_2D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_3D(uint64_t p1_id, uint64_t p2_id, std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);

            if (!(is_3d(n1->type) && is_point(n1->type)) ||
                !(is_3d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Both arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_MID;
            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);

            node.solver = SolveMidPoint_2P_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateMidPoint_3D(uint64_t segment_id, std::string node_name = "Unnamed")
        {
            Node<T>* seg_node = GetNode(segment_id);

            if (seg_node->type != NodeType::LINE_3D_SEGMENT)
            {
                throw std::invalid_argument("Argument must be a 3D Segment object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_MID;
            node.parents.emplace_back(segment_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            seg_node->children.emplace_back(new_id);

            node.solver = SolveMidPoint_Segment_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;

            return new_id;
        }


        uint64_t CreateSnappedPoint_2D(uint64_t target_id, T gx, T gy, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_SNAP;
            node.parents.emplace_back(target_id);

            node.data.snap_2d.guess_x = gx;
            node.data.snap_2d.guess_y = gy;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            GetNode(target_id)->children.emplace_back(new_id);

            node.solver = SolveSnappedPoint_2D<T>;
            node.plotter = PlotPoint_2D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        uint64_t CreateSnappedPoint_3D(uint64_t target_id, T gx, T gy, T gz, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_3D_SNAP;
            node.parents.emplace_back(target_id);

            node.data.snap_3d.guess_x = gx;
            node.data.snap_3d.guess_y = gy;
            node.data.snap_3d.guess_z = gz;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            GetNode(target_id)->children.emplace_back(new_id);

            node.solver = SolveSnappedPoint_3D<T>;
            node.plotter = PlotPoint_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            node.name = node_name;
            return new_id;
        }


        std::vector<uint64_t> GroupCreatePolyGen_2D(uint64_t center_id, T side_length, size_t num_sides,
                                                    std::string node_name = "Unnamed")
        {
            std::vector<uint64_t> created_ids;
            if (num_sides < 3) return created_ids;


            Node<T>* center_node = GetNode(center_id);
            T cx = center_node->data.point_2d.x;
            T cy = center_node->data.point_2d.y;


            const T PI = static_cast<T>(3.14159265358979323846);
            T circumradius = side_length / (static_cast<T>(2.0) * std::sin(PI / static_cast<T>(num_sides)));


            std::vector<uint64_t> vertex_ids;
            vertex_ids.reserve(num_sides);
            created_ids.reserve(num_sides * 2);


            for (size_t i = 0; i < num_sides; ++i)
            {
                T theta = (static_cast<T>(2.0) * PI * static_cast<T>(i)) / static_cast<T>(num_sides);
                T px = cx + circumradius * std::cos(theta);
                T py = cy + circumradius * std::sin(theta);


                uint64_t pid = CreateFreePoint_2D(px, py, node_name);
                vertex_ids.emplace_back(pid);
                created_ids.emplace_back(pid);
            }


            for (size_t i = 0; i < num_sides; ++i)
            {
                uint64_t p_start = vertex_ids[i];
                uint64_t p_end = vertex_ids[(i + 1) % num_sides];


                uint64_t sid = CreateSegment_2D(p_start, p_end, node_name);
                created_ids.emplace_back(sid);
            }

            return created_ids;
        }


        uint64_t CreatePolyGen_2D(uint64_t center_id, T side_length, uint32_t num_sides,
                                  std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!is_2d(center_node->type) || !is_point(center_node->type))
            {
                throw std::invalid_argument("Center must be a 2D Point.");
            }


            auto& node = node_pool.emplace_back();
            node.type = NodeType::POLY_2D_GENERATIVE;
            node.name = node_name;
            node.parents.emplace_back(center_id);


            node.data.poly_gen_2d.side_len = side_length;
            node.data.poly_gen_2d.n = num_sides;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;


            center_node->children.emplace_back(new_id);


            node.solver = SolvePolyGen_2D<T>;
            node.plotter = PlotPolyGen_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateTangent_Diagram_2D(uint64_t curve_id, uint64_t pivot_point_id, std::string node_name = "Unnamed")
        {
            Node<T>* curve = GetNode(curve_id);
            Node<T>* pivot = GetNode(pivot_point_id);

            auto& node = node_pool.emplace_back();
            node.type = NodeType::LINE_2D_TANGENT;
            node.name = node_name;


            node.parents.emplace_back(curve_id);
            node.parents.emplace_back(pivot_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            curve->children.emplace_back(new_id);
            pivot->children.emplace_back(new_id);


            node.solver = SolveTangent_Diagram_2D<T>;
            node.plotter = PlotStraightLine_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }


        uint64_t CreatePlaneInfinity_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id,
                                        std::string node_name = "Unnamed")
        {
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);
            Node<T>* n3 = GetNode(p3_id);

            if (!(is_3d(n1->type) && is_point(n1->type)) ||
                !(is_3d(n2->type) && is_point(n2->type)) ||
                !(is_3d(n3->type) && is_point(n3->type)))
            {
                throw std::invalid_argument("All three arguments must be 3D Point objects.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_3D;
            node.name = node_name;

            node.parents.emplace_back(p1_id);
            node.parents.emplace_back(p2_id);
            node.parents.emplace_back(p3_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            n1->children.emplace_back(new_id);
            n2->children.emplace_back(new_id);
            n3->children.emplace_back(new_id);


            node.solver = SolvePlaneInfinity_3P_3D<T>;

            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }


        uint64_t CreateTangentPlane_3D(uint64_t target_obj_id, uint64_t pivot_point_id,
                                       std::string node_name = "Unnamed")
        {
            Node<T>* obj = GetNode(target_obj_id);
            Node<T>* pivot = GetNode(pivot_point_id);

            if (!is_3d(obj->type) || !is_3d(pivot->type) || !is_point(pivot->type))
            {
                throw std::invalid_argument("Requires a 3D object and a 3D pivot point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::PLANE_TANGENT_3D;
            node.name = node_name;

            node.parents.emplace_back(target_obj_id);
            node.parents.emplace_back(pivot_point_id);

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            obj->children.emplace_back(new_id);
            pivot->children.emplace_back(new_id);


            node.solver = SolveTangentPlane_Diagram_3D<T>;

            node.plotter = PlotPlane_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        std::vector<uint64_t> GroupCreatePlatonicSolid_3D(
            uint64_t center_id,
            T radius,
            int type,
            std::string node_name = "Unnamed")
        {
            std::vector<uint64_t> result_ids;


            Node<T>* center_node = GetNode(center_id);
            const T cx = center_node->data.point_3d.x;
            const T cy = center_node->data.point_3d.y;
            const T cz = center_node->data.point_3d.z;


            auto raw = StaticData::PlatonicLibrary<T>::Get(type);
            if (raw.vertices.empty()) return result_ids;


            std::vector<uint64_t> v_ids;
            v_ids.reserve(raw.vertices.size());
            const T eps = std::numeric_limits<T>::epsilon();

            for (const auto& v : raw.vertices)
            {
                T mag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                T scale = (mag > eps) ? (radius / mag) : radius;

                T tx = cx + v[0] * scale;
                T ty = cy + v[1] * scale;
                T tz = cz + v[2] * scale;


                uint64_t pid = CreateFreePoint_3D(tx, ty, tz, node_name);
                v_ids.emplace_back(pid);
                result_ids.emplace_back(pid);
            }


            std::set<std::pair<size_t, size_t>> edge_registry;

            for (const auto& face : raw.faces)
            {
                for (size_t i = 0; i < face.size(); ++i)
                {
                    size_t idx1 = face[i];
                    size_t idx2 = face[(i + 1) % face.size()];


                    size_t low = std::min(idx1, idx2);
                    size_t high = std::max(idx1, idx2);


                    if (edge_registry.insert({low, high}).second)
                    {
                        uint64_t sid = CreateSegment_3D(v_ids[low], v_ids[high], node_name);
                        result_ids.emplace_back(sid);
                    }
                }
            }


            // ==========================================
            // 5. 第三阶段：创建面 (Planes)
            // ==========================================
            for (const auto& face : raw.faces)
            {
                if (face.size() < 3) continue;

                // 方案 A：如果是三角形（正四、八、二十面体），直接创建
                if (face.size() == 3)
                {
                    uint64_t fid = CreateTriangle_3D(v_ids[face[0]], v_ids[face[1]], v_ids[face[2]], node_name);
                    result_ids.push_back(fid);
                }
                // 方案 B：如果是四边形（正六面体/立方体），保持原有的平行四边形面，因为它刚好能完美覆盖
                else if (face.size() == 4)
                {
                    uint64_t fid = CreatePlane_3D(v_ids[face[0]], v_ids[face[1]], v_ids[face.back()], node_name);
                    result_ids.push_back(fid);
                }
                // 方案 C：如果是五边形（正十二面体）或更多边形，必须切分成三角形扇，防止平面溢出！
                else
                {
                    // 一个 N 边形可以切分成 N - 2 个三角形
                    // 以 face[0] 为公共顶点，依次连接后续顶点
                    for (size_t k = 1; k < face.size() - 1; ++k)
                    {
                        uint64_t fid = CreateTriangle_3D(v_ids[face[0]], v_ids[face[k]], v_ids[face[k + 1]], node_name);
                        result_ids.push_back(fid);
                    }
                }
            }

            return result_ids;
        }


        /**
         * @brief 创建单节点模式的柏拉图多面体
         * @param center_id 中心点 ID
         * @param radius 半径
         * @param type 类型 (4, 6, 8, 12, 20)
         * @param node_name 节点名称
         */
        uint64_t CreatePlatonicSolid_3D(uint64_t center_id, T radius, int type, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!is_3d(center_node->type) || !is_point(center_node->type)) {
                throw std::invalid_argument("Center must be a 3D Point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D; // 借用球体类型或自定义一个新位
            node.name = node_name;
            node.parents.push_back(center_id);

            // 存储生成参数
            node.data.platonic_solid_3d.radius = radius;
            node.data.platonic_solid_3d.type = type;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            center_node->children.push_back(new_id);

            // 绑定专用生成器
            node.solver = SolvePlatonicSolid_3D<T>;
            node.plotter = PlotPlatonicSolid_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateSphere_3D(uint64_t center_id, T radius, std::string node_name = "Unnamed")
        {
            Node<T>* center_node = GetNode(center_id);
            if (!(is_3d(center_node->type) && is_point(center_node->type))) {
                throw std::invalid_argument("Center must be a 3D Point object.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D;
            node.name = node_name;
            node.parents.push_back(center_id);

            // 存入半径
            node.data.sphere_3d.r = radius;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            center_node->children.push_back(new_id);

            // 绑定 Solver 和 Plotter
            node.solver  = SolveSphere_3D<T>;
            node.plotter = PlotSphere_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }
        /**
         * @brief 创建 3D 球体 (四点定球)
         * @param p1_id, p2_id, p3_id, p4_id 四个顶点的 ID
         */
        uint64_t CreateSphere_3D(uint64_t p1_id, uint64_t p2_id, uint64_t p3_id, uint64_t p4_id, std::string node_name = "Unnamed")
        {
            // 1. 获取并校验四个点
            Node<T>* ns[4] = { GetNode(p1_id), GetNode(p2_id), GetNode(p3_id), GetNode(p4_id) };
            for(auto & n : ns) {
                if (!(is_3d(n->type) && is_point(n->type))) {
                    throw std::invalid_argument("All arguments must be 3D Point objects.");
                }
            }

            // 2. 创建球体节点
            auto& node = node_pool.emplace_back();
            node.type = NodeType::SPHERE_3D;
            node.name = node_name;

            // 建立 4 个父节点依赖
            for(auto & n : ns) {
                node.parents.push_back(n->id);
                n->children.push_back(node.id);
            }

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 3. 绑定四点定球专用求解器
            node.solver  = SolveSphere_4P_3D<T>;
            node.plotter = PlotSphere_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        uint64_t CreateIntersectionPoint_2D(const std::vector<uint64_t>& parent_ids, T gx, T gy, std::string node_name = "Unnamed")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::POINT_2D_INTERSECT;
            node.name = node_name;

            // 没有任何限制，直接压入所有 ID
            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(node.id);
            }

            node.data.snap_2d.guess_x = gx;
            node.data.snap_2d.guess_y = gy;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            node.solver = SolveIntersectionPoint_2D<T>;
            node.plotter = PlotPoint_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        /**
         * @brief 创建 3D 万能求交曲线/区域
         * @param parent_ids 参与求交的对象 ID 集合
         */
        uint64_t CreateIntersectionCurve_3D(const std::vector<uint64_t>& parent_ids, std::string node_name = "Unnamed")
        {
            if (parent_ids.size() < 2) {
                throw std::invalid_argument("IntersectionCurve requires at least 2 target nodes.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::UNKNOWN; // 可自定义一个 INTERSECT_CURVE 类型
            node.name = node_name;

            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(node.id);
            }

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 核心设计：仅绑定 Solver，没有 Plotter
            node.solver = SolveIntersectionCurve_3D<T>;
            node.plotter = nullptr;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }



        uint64_t CreateRectangle_2D(uint64_t center_id, T width, T height, std::string node_name = "Unnamed") {
            Node<T>* center_node = GetNode(center_id);

            // 校验输入：中心点必须是 2D 点
            if (!is_2d(center_node->type) || !is_point(center_node->type)) {
                throw std::invalid_argument("Rectangle center must be a 2D Point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::RECT_2D;
            node.name = node_name;
            node.parents.emplace_back(center_id);

            // 存储初始尺寸
            node.data.rect_2d.width = width;
            node.data.rect_2d.height = height;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 建立父子依赖
            center_node->children.emplace_back(new_id);

            // 绑定 Solver 和 Plotter
            node.solver = SolveRectangle_2D<T>;
            node.plotter = PlotRectangle_2D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }



        std::vector<uint64_t> GroupCreateRectangle_2D(uint64_t center_id, T width, T height,
                                                      std::string node_name = "Unnamed_Rect")
        {
            std::vector<uint64_t> created_ids;
            created_ids.reserve(8); // 4点 + 4线

            // 1. 获取中心点当前的物理坐标
            Node<T>* center_node = GetNode(center_id);
            T cx = center_node->data.point_2d.x;
            T cy = center_node->data.point_2d.y;

            // 2. 计算四个顶点的坐标
            T half_w = width / static_cast<T>(2.0);
            T half_h = height / static_cast<T>(2.0);

            T x_min = cx - half_w;
            T x_max = cx + half_w;
            T y_min = cy - half_h;
            T y_max = cy + half_h;

            // 3. 创建四个自由点节点 (顶点)
            // 顺序：左下 -> 右下 -> 右上 -> 左上
            uint64_t v0 = CreateFreePoint_2D(x_min, y_min, node_name + "_V0");
            uint64_t v1 = CreateFreePoint_2D(x_max, y_min, node_name + "_V1");
            uint64_t v2 = CreateFreePoint_2D(x_max, y_max, node_name + "_V2");
            uint64_t v3 = CreateFreePoint_2D(x_min, y_max, node_name + "_V3");

            created_ids.push_back(v0);
            created_ids.push_back(v1);
            created_ids.push_back(v2);
            created_ids.push_back(v3);

            // 4. 创建四条线段节点连接顶点
            uint64_t s0 = CreateSegment_2D(v0, v1, node_name + "_Edge0");
            uint64_t s1 = CreateSegment_2D(v1, v2, node_name + "_Edge1");
            uint64_t s2 = CreateSegment_2D(v2, v3, node_name + "_Edge2");
            uint64_t s3 = CreateSegment_2D(v3, v0, node_name + "_Edge3");

            created_ids.push_back(s0);
            created_ids.push_back(s1);
            created_ids.push_back(s2);
            created_ids.push_back(s3);

            return created_ids;
        }


        uint64_t CreateCuboid_3D(uint64_t center_id, T dx, T dy, T dz, std::string node_name = "Unnamed_Cuboid") {
            Node<T>* center_node = GetNode(center_id);

            if (!is_3d(center_node->type) || !is_point(center_node->type)) {
                throw std::invalid_argument("Cuboid center must be a 3D Point.");
            }

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CUBOID_3D;
            node.name = node_name;
            node.parents.emplace_back(center_id);

            // 存储延伸参数
            node.data.cuboid_3d.dx = dx;
            node.data.cuboid_3d.dy = dy;
            node.data.cuboid_3d.dz = dz;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            center_node->children.emplace_back(new_id);

            // 绑定专用求解器与打点器
            node.solver = SolveCuboid_3D<T>;
            node.plotter = PlotCuboid_3D<T>;
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }

        /**
 * @brief 组合模式创建 3D 长方体
 *
 * 生成结构：
 * - 8 个 Point3D_Free (顶点)
 * - 12 个 Line3D_Segment (棱边)
 * - 12 个 Triangle_3D (面，每个矩形面拆分为2个三角形)
 *
 * @return std::vector<uint64_t> 包含所有生成的子节点 ID [8点, 12线, 12面]
 */
std::vector<uint64_t> GroupCreateCuboid_3D(uint64_t center_id, T dx, T dy, T dz,
                                           std::string node_name = "Unnamed_GroupCuboid")
{
    std::vector<uint64_t> ids;
    ids.reserve(32); // 8 + 12 + 12

    // 1. 获取中心点位置
    Node<T>* center_node = GetNode(center_id);
    T cx = center_node->data.point_3d.x;
    T cy = center_node->data.point_3d.y;
    T cz = center_node->data.point_3d.z;

    // 2. 创建 8 个顶点 (POINT_3D_FREE)
    // 索引约定：0-3 为底面 (z-dz)，4-7 为顶面 (z+dz)
    uint64_t v[8];
    v[0] = CreateFreePoint_3D(cx - dx, cy - dy, cz - dz, node_name);
    v[1] = CreateFreePoint_3D(cx + dx, cy - dy, cz - dz, node_name);
    v[2] = CreateFreePoint_3D(cx + dx, cy + dy, cz - dz, node_name);
    v[3] = CreateFreePoint_3D(cx - dx, cy + dy, cz - dz, node_name);
    v[4] = CreateFreePoint_3D(cx - dx, cy - dy, cz + dz, node_name);
    v[5] = CreateFreePoint_3D(cx + dx, cy - dy, cz + dz, node_name);
    v[6] = CreateFreePoint_3D(cx + dx, cy + dy, cz + dz, node_name);
    v[7] = CreateFreePoint_3D(cx - dx, cy + dy, cz + dz, node_name);

    for (int i = 0; i < 8; ++i) ids.push_back(v[i]);

    // 3. 创建 12 条棱边 (LINE_3D_SEGMENT)
    auto add_edge = [&](int i, int j) {
        ids.push_back(CreateSegment_3D(v[i], v[j], node_name));
    };
    // 底面 4 条
    add_edge(0, 1); add_edge(1, 2); add_edge(2, 3); add_edge(3, 0);
    // 顶面 4 条
    add_edge(4, 5); add_edge(5, 6); add_edge(6, 7); add_edge(7, 4);
    // 垂直 4 条
    add_edge(0, 4); add_edge(1, 5); add_edge(2, 6); add_edge(3, 7);

    // 4. 创建 12 个三角形面 (PLANE_3D_TRIANGLE)
    // 每个矩形面由 2 个三角形组成
    auto add_rect_face = [&](int i, int j, int k, int l) {
        // 三角形 1
        ids.push_back(CreateTriangle_3D(v[i], v[j], v[k], node_name));
        // 三角形 2
        ids.push_back(CreateTriangle_3D(v[i], v[k], v[l], node_name));
    };

    add_rect_face(0, 3, 2, 1); // 底面
    add_rect_face(4, 5, 6, 7); // 顶面
    add_rect_face(0, 1, 5, 4); // 前面
    add_rect_face(1, 2, 6, 5); // 右面
    add_rect_face(2, 3, 7, 6); // 后面
    add_rect_face(3, 0, 4, 7); // 左面

    return ids;
}

        uint64_t CreateCone_3D(uint64_t apex_id, uint64_t base_center_id, T radius, std::string node_name = "Unnamed") {
            Node<T>* n_apex = GetNode(apex_id);
            Node<T>* n_base = GetNode(base_center_id);

            if (!is_3d(n_apex->type) || !is_3d(n_base->type))
                throw std::invalid_argument("Apex and Base Center must be 3D Points.");

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CONE_3D;
            node.name = node_name;
            node.parents.push_back(apex_id);
            node.parents.push_back(base_center_id);
            node.data.cone_3d.r = radius;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n_apex->children.push_back(new_id);
            n_base->children.push_back(new_id);

            node.solver = SolveCone_3D<T>;
            node.plotter = PlotCone_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            return new_id;
        }


        /**
         * @brief 创建 3D 圆柱面节点 (端点 A 到 端点 B 模式)
         *
         * @param p1_id 第一个端点（中心点）的 ID
         * @param p2_id 第二个端点（中心点）的 ID
         * @param radius 圆柱半径 R
         * @param node_name 节点名称
         * @return uint64_t 新创建的圆柱节点 ID
         */
        uint64_t CreateCylinder_3D(uint64_t p1_id, uint64_t p2_id, T radius, std::string node_name = "Unnamed")
        {
            // 1. 获取并校验两个父节点（端点）
            Node<T>* n1 = GetNode(p1_id);
            Node<T>* n2 = GetNode(p2_id);

            // 校验：必须都是 3D 点
            if (!(is_3d(n1->type) && is_point(n1->type)) ||
                !(is_3d(n2->type) && is_point(n2->type)))
            {
                throw std::invalid_argument("Cylinder endpoints must be 3D Point objects.");
            }

            // 2. 在内存池中创建新节点
            auto& node = node_pool.emplace_back();
            node.type = NodeType::CYLINDER_3D;
            node.name = node_name;

            // 3. 建立依赖关系 (Parents/Children)
            // 圆柱节点依赖于这两个点
            node.parents.push_back(p1_id);
            node.parents.push_back(p2_id);

            // 存储半径参数
            node.data.cylinder_3d.r = radius;

            // 4. 注册 ID 映射
            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 将圆柱节点添加为两个端点节点的子节点
            n1->children.push_back(new_id);
            n2->children.push_back(new_id);

            // 5. 绑定求解器与打点器
            // 注意：对应的 SolveCylinder_3D 和 PlotCylinder_3D 需在 solver.hpp 和 plotter.hpp 中实现
            node.solver  = SolveCylinder_3D<T>;
            node.plotter = PlotCylinder_3D<T>;

            // 6. 标记为脏，确保下次 Compute 时执行计算
            node.set_mask(NodeMask::DIRTY);

            return new_id;
        }


        uint64_t CreateCircle_3D(uint64_t center_id, uint64_t normal_pt_id, T radius, std::string node_name = "Unnamed") {
            Node<T>* n_center = GetNode(center_id);
            Node<T>* n_normal = GetNode(normal_pt_id);

            if (!is_3d(n_center->type) || !is_3d(n_normal->type))
                throw std::invalid_argument("Center and Normal Point must be 3D Points.");

            auto& node = node_pool.emplace_back();
            node.type = NodeType::CIRCLE_3D;
            node.name = node_name;
            node.parents.push_back(center_id);
            node.parents.push_back(normal_pt_id);
            node.data.circle_3d.r = radius;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            n_center->children.push_back(new_id);
            n_normal->children.push_back(new_id);

            node.solver = SolveCircle_3D<T>;
            node.plotter = PlotCircle_3D<T>;
            node.set_mask(NodeMask::DIRTY);
            return new_id;
        }
        /**
         * @brief 修改指定的 2D 点坐标
         * @param id 点节点的 ID
         * @param x 新的 X 坐标
         * @param y 新的 Y 坐标
         */
        void ModifyPoint_2D(uint64_t id, T x, T y) {
            Node<T>* node = GetNode(id);

            // 类型校验：确保是 2D 点分类
            if (!is_2d(node->type) || !is_point(node->type)) {
                throw std::invalid_argument("ModifyPoint_2D: Node ID is not a 2D point.");
            }

            // 更新数据
            node->data.point_2d.x = x;
            node->data.point_2d.y = y;

            // 核心：设置脏标记。Graph::Compute 会自动把这个标记传给所有子节点
            node->set_mask(NodeMask::DIRTY);
        }

        /**
         * @brief 修改指定的 3D 点坐标
         * @param id 点节点的 ID
         * @param x, y, z 新坐标
         */
        void ModifyPoint_3D(uint64_t id, T x, T y, T z) {
            Node<T>* node = GetNode(id);

            // 类型校验
            if (!is_3d(node->type) || !is_point(node->type)) {
                throw std::invalid_argument("ModifyPoint_3D: Node ID is not a 3D point.");
            }

            // 更新数据
            node->data.point_3d.x = x;
            node->data.point_3d.y = y;
            node->data.point_3d.z = z;

            // 设置脏标记
            node->set_mask(NodeMask::DIRTY);
        }



        uint64_t CreateBlackBox_3D(const std::vector<uint64_t>& parent_ids,
                               SolverFuncPtr<T> solver,
                               PlotterFuncPtr<T> plotter,
                               std::string node_name = "UserBlackBox_3D")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::BLACKBOX_3D;
            node.name = node_name;
            node.solver = solver;
            node.plotter = plotter;

            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;

            // 绑定父子关系
            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(new_id);
            }

            node.set_mask(NodeMask::DIRTY);
            return new_id;
        }

        // 2D 版本同理
        uint64_t CreateBlackBox_2D(const std::vector<uint64_t>& parent_ids,
                                   SolverFuncPtr<T> solver,
                                   PlotterFuncPtr<T> plotter,
                                   std::string node_name = "UserBlackBox_2D")
        {
            auto& node = node_pool.emplace_back();
            node.type = NodeType::BLACKBOX_2D;
            node.name = node_name;
            node.solver = solver;
            node.plotter = plotter;
            uint64_t new_id = node.id;
            id_map[new_id] = node_pool.size() - 1;
            for (uint64_t pid : parent_ids) {
                node.parents.push_back(pid);
                GetNode(pid)->children.push_back(new_id);
            }
            node.set_mask(NodeMask::DIRTY);
            return new_id;
        }
    };
}
