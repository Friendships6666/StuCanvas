/***************************************************************************
 * Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
 *                                                                          *
 * Distributed under the terms of the MIT License.                          *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ***************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <vector>

#include "flex_vector.hpp"
#include "instance.hpp"
#include "object.hpp"
#include "pinned_vector.hpp"
#include "tiny_vector.hpp"

 
namespace StuCanvas
{
    struct DAGObject;
    struct DAGraph;
    inline void DAGObjectSolver ( DAGraph&, DAGObject& );
    // 🚀 链式查询辅助类（在内存中进行局部极速筛选，0-Allocation 中间状态）
    struct GraphQuery
    {
    private:

        utils::TinyVector< DAGObject* > m_candidates;

    public:

        GraphQuery () noexcept = default;

        explicit GraphQuery ( utils::TinyVector< DAGObject* >&& candidates ) noexcept
            : m_candidates ( std::move ( candidates ) )
        {
        }

        // 链式过滤：按名称筛选（原地复用 TinyVector 内存，不产生 STL 分配）
        inline GraphQuery& findByName ( std::string_view name )
        {
            utils::TinyVector< DAGObject* > filtered;
            for ( DAGObject* node : m_candidates )
            {
                if ( static_cast< std::string_view > ( node->name ) == name )
                {
                    filtered.push_back ( node );
                }
            }
            m_candidates = std::move ( filtered );
            return *this;
        }

        // 链式过滤：按用户自定义 ID 筛选
        inline GraphQuery& findByID ( uint32_t id )
        {
            utils::TinyVector< DAGObject* > filtered;
            for ( DAGObject* node : m_candidates )
            {
                if ( node->id == id )
                {
                    filtered.push_back ( node );
                }
            }
            m_candidates = std::move ( filtered );
            return *this;
        }

        // 链式过滤：按类型筛选
        inline GraphQuery& findByType ( NodeType type )
        {
            utils::TinyVector< DAGObject* > filtered;
            for ( DAGObject* node : m_candidates )
            {
                if ( node->type == type )
                {
                    filtered.push_back ( node );
                }
            }
            m_candidates = std::move ( filtered );
            return *this;
        }

        // 终结器：将最终结果一次性拷贝到外部 C# 或 C++ 接口熟悉的标准容器中返回
        [[nodiscard]] inline std::vector< DAGObject* > findEnd ()
        {
            std::vector< DAGObject* > result;
            result.reserve ( m_candidates.size () );
            for ( DAGObject* node : m_candidates )
            {
                result.push_back ( node );
            }
            return result;
        }
    };

    struct DAGObjectInstance;

    struct DAGraph
    {
    private:

        std::bitset< 64 > flag;
        utils::TinyVector< utils::TinyVector< DAGObject* > > dirty_double_list;

        utils::PinnedVector< DAGObject, 32 > node_pool;
        utils::TinyVector< DAGObject* > dirty_nodes;
        utils::PinnedVector< DAGObjectInstance, 32 > instance_pool;
        utils::FlexVector<> appearance_pool;


        inline DAGObjectInstance& createInstance ( DAGObject& object )
        {
            DAGObjectInstance& instance = instance_pool.emplace_back ();
            instance.source = &object;
            object.instances.emplace_back ( &instance );
            return instance;
        }

        // 🚀 辅助函数：判断 TinyVector 中是否已包含某个节点指针（L1 缓存极速扫视）
        [[nodiscard]] inline bool contains ( const utils::TinyVector< DAGObject* >& vec, DAGObject* val ) const noexcept
        {
            for ( const DAGObject* item : vec )
            {
                if ( item == val )
                {
                    return true;
                }
            }
            return false;
        }

        // 🚀 递归 DFS：自上而下仅沿着脏节点进行下游脏状态拓扑收集
        inline void dfs ( DAGObject* node, utils::TinyVector< DAGObject* >& temp_visited,
                          utils::TinyVector< DAGObject* >& flat_topo )
        {
            if ( contains ( temp_visited, node ) )
            {
                return;
            }
            temp_visited.push_back ( node );

            for ( DAGObject* child : node->children )
            {
                dfs ( child, temp_visited, flat_topo );
            }

            flat_topo.push_back ( node );
        }

        // 🚀 核心：利用成员变量 dirty_double_list 以及并行数组，进行无指针追逐的 ASAP 脏双重表构建
        inline utils::TinyVector< utils::TinyVector< DAGObject* > >& buildDirtyDoubleList ()
        {
            dirty_double_list.clear ();

            if ( dirty_nodes.empty () )
            {
                return dirty_double_list;
            }

            utils::TinyVector< DAGObject* > temp_visited;
            utils::TinyVector< DAGObject* > flat_topo;

            for ( DAGObject* node : dirty_nodes )
            {
                dfs ( node, temp_visited, flat_topo );
            }

            if ( !flat_topo.empty () )
            {
                uint32_t left = 0;
                uint32_t right = flat_topo.size () - 1;
                while ( left < right )
                {
                    std::swap ( flat_topo[ left ], flat_topo[ right ] );
                    left++;
                    right--;
                }
            }

            // 声明并对齐并行相对 Rank 状态数组，消除 nested vector 间接寻址导致的 Cache Miss
            utils::TinyVector< uint32_t > ranks;
            ranks.resize ( flat_topo.size (), 0 );

            for ( size_t i = 0; i < flat_topo.size (); ++i )
            {
                DAGObject* node = flat_topo[ i ];
                uint32_t target_rank = 0;

                for ( DAGObject* parent : node->parents )
                {
                    // 顺着连续一重拓扑内存反向寻找父节点对应层级，拓扑相邻性保障 L1/L2 极速命中
                    for ( int j = static_cast< int > ( i ) - 1; j >= 0; --j )
                    {
                        if ( flat_topo[ j ] == parent )
                        {
                            target_rank = std::max ( target_rank, ranks[ j ] + 1 );
                            break;
                        }
                    }
                }

                ranks[ i ] = target_rank;

                if ( target_rank >= dirty_double_list.size () )
                {
                    dirty_double_list.resize ( target_rank + 1 );
                }
                dirty_double_list[ target_rank ].push_back ( node );
            }

            return dirty_double_list;
        }

        inline DAGObject& allocateDirtyNode ()
        {
            auto& new_node = node_pool.emplace_back ();
            dirty_nodes.emplace_back ( &new_node );
            new_node.graph = this;

            return new_node;
        }

        inline void appendParents ( DAGObject& object, std::span< DAGObject* > parents )
        {
            object.parents.append ( parents );
            for ( DAGObject* parent : parents )
            {
                parent->children.push_back ( &object );
            }
        }

        inline void setParents ( DAGObject& object, std::span< DAGObject* > parents )
        {
            for ( DAGObject* old_parent : object.parents )
            {
                old_parent->children.erase_unordered ( &object );
            }

            object.parents.clear ();
            object.parents.append ( parents );

            for ( DAGObject* parent : parents )
            {
                parent->children.push_back ( &object );
            }
        }

        inline void markDirty ( DAGObject& node )
        {
            dirty_nodes.push_back ( &node );
        }

    public:

        // 🚀 启动链式查询（通过用户自定义 ID 初始化候选池）
        [[nodiscard]] inline GraphQuery findByIDQuery ( uint32_t id )
        {
            utils::TinyVector< DAGObject* > candidates;
            for ( auto& node : node_pool )
            {
                if ( node.id == id )
                {
                    candidates.push_back ( &node );
                }
            }
            return GraphQuery ( std::move ( candidates ) );
        }

        // 🚀 启动链式查询：通过名称初始化候选池
        [[nodiscard]] inline GraphQuery findByName ( std::string_view name )
        {
            utils::TinyVector< DAGObject* > candidates;
            for ( auto& node : node_pool )
            {
                if ( static_cast< std::string_view > ( node.name ) == name )
                {
                    candidates.push_back ( &node );
                }
            }
            return GraphQuery ( std::move ( candidates ) );
        }

        // 🚀 启动链式查询：通过类型初始化候选池
        [[nodiscard]] inline GraphQuery findByType ( NodeType type )
        {
            utils::TinyVector< DAGObject* > candidates;
            for ( auto& node : node_pool )
            {
                if ( node.type == type )
                {
                    candidates.push_back ( &node );
                }
            }
            return GraphQuery ( std::move ( candidates ) );
        }

        // 🚀 直接通过 ID 快速查找单个可变节点（未找到返回 nullptr）
        [[nodiscard]] inline DAGObject* findByID ( uint32_t id ) noexcept
        {
            for ( auto& node : node_pool )
            {
                if ( node.id == id )
                {
                    return &node;
                }
            }
            return nullptr;
        }

        // 🚀 极致性能评估函数（带延迟去重、可变引用解算与 O(D) 属性清理）
        void evaluate ()
        {
            if ( dirty_nodes.empty () )
            {
                return;
            }

            // 🚀 1. 延迟一键去重：利用连续内存进行极速排序 + 去重，消除高频 markDirty 中的线性 contains 压力
            std::sort ( dirty_nodes.begin (), dirty_nodes.end () );
            auto* new_end = std::unique ( dirty_nodes.begin (), dirty_nodes.end () );
            dirty_nodes.erase ( new_end, dirty_nodes.end () );

            // 2. 极速构建脏双重表
            buildDirtyDoubleList ();

            // 3. 顺序解算脏双重列表（每一层为一个同一个 Rank，完全支持并行/多线程解算）
            for ( auto& rank_list : dirty_double_list )
            {
                for ( DAGObject* node : rank_list )
                {
                    // 调用外部物理求值器（传入可变引用，0 虚函数表开销）
                    DAGObjectSolver ( *this, *node );

                    // 标记该节点在当前求值纪元中已完成解算
                    node->flag.set ( static_cast< size_t > ( NodeProperty::Solved ) );
                }
            }

            // 🚀 4. 全部解算结束后，局部遍历脏双重表，一键清洗已求值节点的 Solved 标记，恢复干净状态
            //    只访问受波及的子图（O(D) 复杂度），绝对不进行 O(N) 的 node_pool 全图重置
            for ( auto& rank_list : dirty_double_list )
            {
                for ( DAGObject* node : rank_list )
                {
                    node->flag.reset ( static_cast< size_t > ( NodeProperty::Solved ) );
                }
            }

            // 5. 清空脏节点队列，等待下一次属性/关系变更触发
            dirty_nodes.clear ();
        }
        inline void modifyName ( DAGObject& node, std::string_view new_name )
        {
            node.name = new_name;
        }

        inline void modifyID ( DAGObject& node, uint32_t new_id )
        {
            node.id = new_id;
        }

        inline void modifyParents ( DAGObject& node, std::span< DAGObject* > parents )
        {
            setParents ( node, parents );
            markDirty ( node );
        }

        inline void modifyFreePoint2D ( DAGObject& node, double x, double y )
        {
            node.data.point_2d.x = x;
            node.data.point_2d.y = y;

            markDirty ( node );
        }

        inline void modifyFreePoint3D ( DAGObject& node, double x, double y, double z )
        {
            node.data.point_3d.x = x;
            node.data.point_3d.y = y;
            node.data.point_3d.z = z;

            markDirty ( node );
        }

        inline void modifyScalar ( DAGObject& node, double value )
        {
            node.data.scalar.value = value;

            markDirty ( node );
        }

        inline void modifySnapGuess2D ( DAGObject& node, double guess_x, double guess_y )
        {
            node.data.snap_2d.x = guess_x;
            node.data.snap_2d.y = guess_y;
            node.data.snap_2d.lock = -1.0;

            markDirty ( node );
        }

        inline void modifySnapGuess3D ( DAGObject& node, double guess_x, double guess_y, double guess_z )
        {
            node.data.snap_3d.x = guess_x;
            node.data.snap_3d.y = guess_y;
            node.data.snap_3d.z = guess_z;
            node.data.snap_3d.a = -1.0;
            node.data.snap_3d.b = -1.0;

            markDirty ( node );
        }

        DAGObject& createFreePoint2D ( double x, double y )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_2D_FREE;
            node.name = "FreePoint2d";
            node.data.point_2d.x = x;
            node.data.point_2d.y = y;
            return node;
        }

        DAGObject& createFreePoint3D ( double x, double y, double z )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_3D_FREE;
            node.name = "FreePoint3d";
            node.data.point_3d.x = x;
            node.data.point_3d.y = y;
            node.data.point_3d.z = z;
            return node;
        }

        DAGObject& createScalar ( double value )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::SCALAR;
            node.name = "Scalar";
            node.data.scalar.value = value;
            return node;
        }

        DAGObject& createSegment2D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_2D_SEGMENT;
            node.name = "Segment2d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createStraightLine2D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_2D_STRAIGHT;
            node.name = "StraightLine2d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createRay2D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_2D_RAY;
            node.name = "Ray2d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createSegment3D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_3D_SEGMENT;
            node.name = "Segment3d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createStraightLine3D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_3D_STRAIGHT;
            node.name = "StraightLine3d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createRay3D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_3D_RAY;
            node.name = "Ray3d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createPlane3D ( DAGObject& p1, DAGObject& p2, DAGObject& p3 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::PLANE_3D;
            node.name = "Plane3d";

            std::array< DAGObject*, 3 > parents = { &p1, &p2, &p3 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createMidPoint2D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_2D_MID;
            node.name = "MidPoint2d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createMidPoint3D ( DAGObject& p1, DAGObject& p2 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_3D_MID;
            node.name = "MidPoint3d";

            std::array< DAGObject*, 2 > parents = { &p1, &p2 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createCircle2D ( DAGObject& center, DAGObject& radius )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::CIRCLE_2D;
            node.name = "Circle2d";

            std::array< DAGObject*, 2 > parents = { &center, &radius };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createCircle2DThreePoints ( DAGObject& p1, DAGObject& p2, DAGObject& p3 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::CIRCLE_2D_THREE_POINTS;
            node.name = "Circle2dThreePoints";

            std::array< DAGObject*, 3 > parents = { &p1, &p2, &p3 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createSphere3D ( DAGObject& center, DAGObject& radius )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::SPHERE_3D;
            node.name = "Sphere3d";

            std::array< DAGObject*, 2 > parents = { &center, &radius };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createSphere3DFourPoints ( DAGObject& p1, DAGObject& p2, DAGObject& p3, DAGObject& p4 )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::SPHERE_3D_FOUR_POINTS;
            node.name = "Sphere3dFourPoints";

            std::array< DAGObject*, 4 > parents = { &p1, &p2, &p3, &p4 };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createCylinder3D ( DAGObject& p1, DAGObject& p2, DAGObject& radius )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::CYLINDER_3D;
            node.name = "Cylinder3d";

            std::array< DAGObject*, 3 > parents = { &p1, &p2, &radius };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createParallelLine2D ( DAGObject& line, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_2D_PARALLEL;
            node.name = "ParallelLine2d";

            std::array< DAGObject*, 2 > parents = { &line, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createPerpendicularLine2D ( DAGObject& line, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_2D_PERPENDICULAR;
            node.name = "PerpendicularLine2d";

            std::array< DAGObject*, 2 > parents = { &line, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createParallelLine3D ( DAGObject& line, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_3D_PARALLEL;
            node.name = "ParallelLine3d";

            std::array< DAGObject*, 2 > parents = { &line, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createPerpendicularLine3D ( DAGObject& line, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::LINE_3D_PERPENDICULAR;
            node.name = "PerpendicularLine3d";

            std::array< DAGObject*, 2 > parents = { &line, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createParallelPlane3D ( DAGObject& plane, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::PLANE_3D_PARALLEL;
            node.name = "ParallelPlane3d";

            std::array< DAGObject*, 2 > parents = { &plane, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createPerpendicularPlane3D ( DAGObject& plane, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::PLANE_3D_PERPENDICULAR;
            node.name = "PerpendicularPlane3d";

            std::array< DAGObject*, 2 > parents = { &plane, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createSnapPoint2D ( DAGObject& target, double guess_x, double guess_y )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_2D_SNAP;
            node.name = "SnapPoint2d";
            node.data.snap_2d.x = guess_x;
            node.data.snap_2d.y = guess_y;
            node.data.snap_2d.lock = -1.0;

            std::array< DAGObject*, 1 > parents = { &target };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createSnapPoint3D ( DAGObject& target, double guess_x, double guess_y, double guess_z )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::POINT_3D_SNAP;
            node.name = "SnapPoint3d";
            node.data.snap_3d.x = guess_x;
            node.data.snap_3d.y = guess_y;
            node.data.snap_3d.z = guess_z;
            node.data.snap_3d.a = -1.0;
            node.data.snap_3d.b = -1.0;

            std::array< DAGObject*, 1 > parents = { &target };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createTangent2D ( DAGObject& curve, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::TANGENT_2D;
            node.name = "Tangent2d";

            std::array< DAGObject*, 2 > parents = { &curve, &point };
            appendParents ( node, parents );
            return node;
        }

        DAGObject& createTangent3D ( DAGObject& curve, DAGObject& point )
        {
            auto& node = allocateDirtyNode ();
            node.type = NodeType::TANGENT_3D;
            node.name = "Tangent3d";

            std::array< DAGObject*, 2 > parents = { &curve, &point };
            appendParents ( node, parents );
            return node;
        }

        // =====================================================================
        // 1. 空间与定义域限制创建接口 (Spatial & Domain Bounds)
        // =====================================================================

        inline void createAssetXDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::xDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetYDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::yDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetZDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::zDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetTDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::tDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetUDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::uDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetVDomain ( DAGObject& node, double min_val, double max_val )
        {
            node.assets.emplace_back< DAGAssets::vDomain > ( min_val, max_val );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetDiscretizationStep ( DAGObject& node, double value )
        {
            node.assets.emplace_back< DAGAssets::DiscretizationStep > ( value );
            dirty_nodes.push_back ( &node );
        }

        // =====================================================================
        // 2. 一元显式标量函数创建接口 (Explicit Unary Scalar Functions)
        // =====================================================================

        inline void createAssetExplicitScalarFnYFromX ( DAGObject& node, std::function< double ( double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitScalarFnYFromX > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitScalarFnXFromY ( DAGObject& node, std::function< double ( double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitScalarFnXFromY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =====================================================================
        // 3. 二元显式标量函数创建接口 (Explicit Binary Scalar Functions)
        // =====================================================================

        inline void createAssetExplicitScalarFnZFromXY ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitScalarFnZFromXY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitScalarFnYFromXZ ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitScalarFnYFromXZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =====================================================================
        // 4. 二元显式标量函数创建接口 (Explicit Binary Scalar Functions)
        // =====================================================================

        inline void createAssetExplicitScalarFnXFromYZ ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitScalarFnXFromYZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =====================================================================
        // 5. 隐式代数函数创建接口 (Implicit Functions)
        // =====================================================================

        inline void createAssetImplicitFn2D ( DAGObject& node, std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitFn2D > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitFn3D ( DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitFn3D > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =====================================================================
        // 6. 参数化函数创建接口 (Parametric Functions)
        // =====================================================================

        inline void createAssetParametricCurve2D ( DAGObject& node, std::function< double ( double ) > x_fn,
                                                   std::function< double ( double ) > y_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricCurve2D > ( std::move ( x_fn ), std::move ( y_fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetParametricCurve3D ( DAGObject& node, std::function< double ( double ) > x_fn,
                                                   std::function< double ( double ) > y_fn,
                                                   std::function< double ( double ) > z_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricCurve3D > ( std::move ( x_fn ), std::move ( y_fn ),
                                                                       std::move ( z_fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetParametricSurface3D ( DAGObject& node, std::function< double ( double, double ) > x_fn,
                                                     std::function< double ( double, double ) > y_fn,
                                                     std::function< double ( double, double ) > z_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricSurface3D > ( std::move ( x_fn ), std::move ( y_fn ),
                                                                         std::move ( z_fn ) );
            dirty_nodes.push_back ( &node );
        }


        // =====================================================================
        // 8. 导数与偏导数创建接口 (Calculus: Derivatives & Partial Derivatives)
        // =====================================================================

        inline void createAssetExplicitDerivativeFnYWrtX ( DAGObject& node, uint32_t order,
                                                           std::function< double ( double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitDerivativeFnYWrtX > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitDerivativeFnXWrtY ( DAGObject& node, uint32_t order,
                                                           std::function< double ( double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitDerivativeFnXWrtY > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnZWrtX ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnZWrtX > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnZWrtY ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnZWrtY > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnYWrtX ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnYWrtX > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnYWrtZ ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnYWrtZ > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnXWrtY ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnXWrtY > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialDerivativeFnXWrtZ ( DAGObject& node, uint32_t order,
                                                                  std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialDerivativeFnXWrtZ > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitDerivativeFnYWrtX2D ( DAGObject& node, uint32_t order,
                                                             std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitDerivativeFnYWrtX2D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitDerivativeFnXWrtY2D ( DAGObject& node, uint32_t order,
                                                             std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitDerivativeFnXWrtY2D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnZWrtX3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnZWrtX3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnZWrtY3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnZWrtY3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnYWrtX3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnYWrtX3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnYWrtZ3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnYWrtZ3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnXWrtY3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnXWrtY3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetImplicitPartialDerivativeFnXWrtZ3D (
            DAGObject& node, uint32_t order, std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ImplicitPartialDerivativeFnXWrtZ3D > ( order, std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =========================================================================
        // 9. 积分、偏积分与多重积分创建接口 (Integrals, Partial & Multiple Integrals)
        // =========================================================================

        inline void createAssetExplicitIntegralFnYFromX ( DAGObject& node,
                                                          std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntegralFnYFromX > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntegralFnXFromY ( DAGObject& node,
                                                          std::function< double ( double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntegralFnXFromY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnZWrtX ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnZWrtX > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnZWrtY ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnZWrtY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnYWrtX ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnYWrtX > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnYWrtZ ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnYWrtZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnXWrtY ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnXWrtY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitPartialIntegralFnXWrtZ ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitPartialIntegralFnXWrtZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetDoubleIntegralFnZFromXY ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::DoubleIntegralFnZFromXY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetDoubleIntegralFnYFromXZ ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::DoubleIntegralFnYFromXZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetDoubleIntegralFnXFromYZ ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::DoubleIntegralFnXFromYZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetTripleIntegralFn3D (
            DAGObject& node, std::function< double ( double, double, double, double, double, double ) > fn )
        {
            node.assets.emplace_back< DAGAssets::TripleIntegralFn3D > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        // =========================================================================
        // 10. 区间算术创建接口 (Interval Arithmetic Functions)
        // =========================================================================

        inline void createAssetExplicitIntervalFnYFromX (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnYFromX > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnXFromY (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnXFromY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnZFromXY (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnZFromXY > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnYFromXZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnYFromXZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnXFromYZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnXFromYZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnWFromXYZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnWFromXYZ > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnZFromXYW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnZFromXYW > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnYFromXZW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnYFromXZW > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetExplicitIntervalFnXFromYZW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            node.assets.emplace_back< DAGAssets::ExplicitIntervalFnXFromYZW > ( std::move ( fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetParametricIntervalCurve2D (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricIntervalCurve2D > ( std::move ( x_fn ), std::move ( y_fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetParametricIntervalCurve3D (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > z_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricIntervalCurve3D > ( std::move ( x_fn ), std::move ( y_fn ),
                                                                               std::move ( z_fn ) );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetParametricIntervalSurface3D (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                          const utils::IntervalSet< double >& ) >
                x_fn,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                          const utils::IntervalSet< double >& ) >
                y_fn,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                          const utils::IntervalSet< double >& ) >
                z_fn )
        {
            node.assets.emplace_back< DAGAssets::ParametricIntervalSurface3D > ( std::move ( x_fn ), std::move ( y_fn ),
                                                                                 std::move ( z_fn ) );
            dirty_nodes.push_back ( &node );
        }
        inline void createAssetCpuCoreCount ( DAGObject& node, uint32_t value )
        {
            node.assets.emplace_back< DAGAssets::CpuCoreCount > ( value );
            dirty_nodes.push_back ( &node );
        }

        inline void createAssetLShade ( DAGObject& node, uint32_t initial_population_size, uint32_t min_population_size,
                                        uint32_t max_evaluations, uint32_t seed )
        {
            node.assets.emplace_back< DAGAssets::LShade > ( initial_population_size, min_population_size,
                                                            max_evaluations, seed );
            dirty_nodes.push_back ( &node );
        }


        // =====================================================================
        // 1. 空间与定义域限制修改接口 (Spatial & Domain Bounds - 简单标量一键整体修改)
        // =====================================================================

        inline void modifyAssetXDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::xDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetYDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::yDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetZDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::zDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetTDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::tDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetUDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::uDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetVDomain ( DAGObject& node, double min_val, double max_val )
        {
            auto* asset = node.assets.get< DAGAssets::vDomain > ();
            asset->min_val = min_val;
            asset->max_val = max_val;
            markDirty ( node );
        }

        inline void modifyAssetDiscretizationStep ( DAGObject& node, double value )
        {
            auto* asset = node.assets.get< DAGAssets::DiscretizationStep > ();
            asset->value = value;
            markDirty ( node );
        }

        // =====================================================================
        // 2. 一元显式标量函数修改接口 (Explicit Unary Scalar Functions)
        // =====================================================================

        inline void modifyAssetExplicitScalarFnYFromX ( DAGObject& node, std::function< double ( double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitScalarFnYFromX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitScalarFnXFromY ( DAGObject& node, std::function< double ( double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitScalarFnXFromY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        // =====================================================================
        // 3. 二元显式标量函数修改接口 (Explicit Binary Scalar Functions)
        // =====================================================================

        inline void modifyAssetExplicitScalarFnZFromXY ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitScalarFnZFromXY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitScalarFnYFromXZ ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitScalarFnYFromXZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitScalarFnXFromYZ ( DAGObject& node,
                                                         std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitScalarFnXFromYZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        // =====================================================================
        // 4. 隐式代数函数修改接口 (Implicit Functions)
        // =====================================================================

        inline void modifyAssetImplicitFn2D ( DAGObject& node, std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitFn2D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitFn3D ( DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitFn3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        // =====================================================================
        // 5. 参数化函数修改接口 (Parametric Functions - 包含多 std::function 的细粒度修改)
        // =====================================================================

        inline void modifyAssetParametricCurve2DX ( DAGObject& node, std::function< double ( double ) > x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricCurve2D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricCurve2DY ( DAGObject& node, std::function< double ( double ) > y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricCurve2D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricCurve3DX ( DAGObject& node, std::function< double ( double ) > x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricCurve3D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricCurve3DY ( DAGObject& node, std::function< double ( double ) > y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricCurve3D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricCurve3DZ ( DAGObject& node, std::function< double ( double ) > z_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricCurve3D > ();
            asset->z_fn = std::move ( z_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricSurface3DX ( DAGObject& node, std::function< double ( double, double ) > x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricSurface3D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricSurface3DY ( DAGObject& node, std::function< double ( double, double ) > y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricSurface3D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricSurface3DZ ( DAGObject& node, std::function< double ( double, double ) > z_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricSurface3D > ();
            asset->z_fn = std::move ( z_fn );
            markDirty ( node );
        }

        // =====================================================================
        // 7. 导数与偏导数修改接口 (Calculus: Derivatives & Partial Derivatives - 细粒度修改)
        // =====================================================================

        inline void modifyAssetExplicitDerivativeFnYWrtXOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitDerivativeFnYWrtX > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitDerivativeFnYWrtXFn ( DAGObject& node, std::function< double ( double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitDerivativeFnYWrtX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitDerivativeFnXWrtYOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitDerivativeFnXWrtY > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitDerivativeFnXWrtYFn ( DAGObject& node, std::function< double ( double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitDerivativeFnXWrtY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnZWrtXOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnZWrtX > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnZWrtXFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnZWrtX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnZWrtYOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnZWrtY > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnZWrtYFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnZWrtY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnYWrtXOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnYWrtX > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnYWrtXFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnYWrtX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnYWrtZOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnYWrtZ > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnYWrtZFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnYWrtZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnXWrtYOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnXWrtY > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnXWrtYFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnXWrtY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnXWrtZOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnXWrtZ > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialDerivativeFnXWrtZFn ( DAGObject& node,
                                                                    std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialDerivativeFnXWrtZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitDerivativeFnYWrtX2DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitDerivativeFnYWrtX2D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitDerivativeFnYWrtX2DFn ( DAGObject& node,
                                                               std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitDerivativeFnYWrtX2D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitDerivativeFnXWrtY2DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitDerivativeFnXWrtY2D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitDerivativeFnXWrtY2DFn ( DAGObject& node,
                                                               std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitDerivativeFnXWrtY2D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnZWrtX3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnZWrtX3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnZWrtX3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnZWrtX3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnZWrtY3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnZWrtY3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnZWrtY3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnZWrtY3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnYWrtX3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnYWrtX3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnYWrtX3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnYWrtX3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnYWrtZ3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnYWrtZ3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnYWrtZ3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnYWrtZ3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnXWrtY3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnXWrtY3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnXWrtY3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnXWrtY3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnXWrtZ3DOrder ( DAGObject& node, uint32_t order )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnXWrtZ3D > ();
            asset->order = order;
            markDirty ( node );
        }

        inline void modifyAssetImplicitPartialDerivativeFnXWrtZ3DFn (
            DAGObject& node, std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ImplicitPartialDerivativeFnXWrtZ3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        // =========================================================================
        // 18. 积分、偏积分与多重积分修改接口 (Integrals, Partial & Multiple Integrals - 简单资产一键修改)
        // =========================================================================

        inline void modifyAssetExplicitIntegralFnYFromX ( DAGObject& node,
                                                          std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntegralFnYFromX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntegralFnXFromY ( DAGObject& node,
                                                          std::function< double ( double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntegralFnXFromY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnZWrtX ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnZWrtX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnZWrtY ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnZWrtY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnYWrtX ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnYWrtX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnYWrtZ ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnYWrtZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnXWrtY ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnXWrtY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitPartialIntegralFnXWrtZ ( DAGObject& node,
                                                                std::function< double ( double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitPartialIntegralFnXWrtZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetDoubleIntegralFnZFromXY ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::DoubleIntegralFnZFromXY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetDoubleIntegralFnYFromXZ ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::DoubleIntegralFnYFromXZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetDoubleIntegralFnXFromYZ ( DAGObject& node,
                                                         std::function< double ( double, double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::DoubleIntegralFnXFromYZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetTripleIntegralFn3D (
            DAGObject& node, std::function< double ( double, double, double, double, double, double ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::TripleIntegralFn3D > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        // =========================================================================
        // 19. 区间算术修改接口 (Interval Arithmetic Functions)
        // =========================================================================

        inline void modifyAssetExplicitIntervalFnYFromX (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnYFromX > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnXFromY (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnXFromY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnZFromXY (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnZFromXY > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnYFromXZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnYFromXZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnXFromYZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnXFromYZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnWFromXYZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnWFromXYZ > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnZFromXYW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnZFromXYW > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnYFromXZW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnYFromXZW > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetExplicitIntervalFnXFromYZW (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 fn )
        {
            auto* asset = node.assets.get< DAGAssets::ExplicitIntervalFnXFromYZW > ();
            asset->fn = std::move ( fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalCurve2DX (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalCurve2D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalCurve2DY (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalCurve2D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalCurve3DX (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalCurve3D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalCurve3DY (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalCurve3D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalCurve3DZ (
            DAGObject& node,
            std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >& ) > z_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalCurve3D > ();
            asset->z_fn = std::move ( z_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalSurface3DX (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 x_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalSurface3D > ();
            asset->x_fn = std::move ( x_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalSurface3DY (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 y_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalSurface3D > ();
            asset->y_fn = std::move ( y_fn );
            markDirty ( node );
        }

        inline void modifyAssetParametricIntervalSurface3DZ (
            DAGObject& node, std::function< utils::IntervalSet< double > ( const utils::IntervalSet< double >&,
                                                                           const utils::IntervalSet< double >& ) >
                                 z_fn )
        {
            auto* asset = node.assets.get< DAGAssets::ParametricIntervalSurface3D > ();
            asset->z_fn = std::move ( z_fn );
            markDirty ( node );
        }
    };
}   // namespace StuCanvas
// namespace StuCanvas
//
//
#include "solver.hpp"
