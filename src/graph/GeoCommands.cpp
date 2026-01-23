// --- src/graph/GeoCommands.cpp ---
#include "../../include/graph/GeoCommands.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotCall.h"
#include <algorithm>
#include <vector>

extern std::vector<PointData> wasm_final_contiguous_buffer;
extern std::vector<FunctionRange> wasm_function_ranges_buffer;

namespace GeoCommand {

    // --- 内部辅助：属性打包与提取 ---

    static void PackVisualConfig(const GeoNode& node, CommandPacket& pkt) {
        const auto& cfg = node.config;
        pkt.name = cfg.name;
        pkt.thickness = cfg.thickness;
        pkt.color = cfg.color;
        pkt.is_visible = cfg.is_visible;
        pkt.show_label = cfg.show_label;
        pkt.label_offset_x = cfg.label_offset_x;
        pkt.label_offset_y = cfg.label_offset_y;
        pkt.label_size = cfg.label_size;
        pkt.label_color = cfg.label_color;
        pkt.state_mask = node.state_mask;
    }

// --- src/graph/GeoCommands.cpp ---

/**
 * @brief 将一个存活的 GeoNode 转换回最初的创建指令（基因提取）
 * 💡 适配最新架构：公式直接从节点自身的 channels 数组中提取
 */
static CommandPacket ExtractNodeToCommand(const GeoNode& node, const GeometryGraph& graph) {
    CommandPacket pkt;
    // 1. 提取所有视觉配置与备用掩码 (name, color, thickness, state_mask 等)
    PackVisualConfig(node, pkt);

    // 2. 根据几何类型提取核心逻辑基因
    switch (node.type) {
        case GeoType::POINT_FREE: {
            pkt.op = (uint8_t)OpCode::CREATE_FREE_POINT;
            // 💡 自由点的 X, Y 公式直接存储在自己的通道 0 和 1 中
            pkt.s0 = node.channels[0].original_infix; // 提取 X 源码
            pkt.s1 = node.channels[1].original_infix; // 提取 Y 源码
            break;
        }

        case GeoType::LINE_SEGMENT: {
            pkt.op = (uint8_t)OpCode::CREATE_SEGMENT_2P;
            // 线段本身不带公式，它存储的是对两个父节点（点）的逻辑 ID 引用
            if (node.parents.size() >= 2) {
                pkt.id0 = node.parents[0];
                pkt.id1 = node.parents[1];
            }
            break;
        }

        case GeoType::POINT_MID: {
            pkt.op = (uint8_t)OpCode::CREATE_MID_POINT;
            // 中点存储对两个父点 ID 的引用
            if (node.parents.size() >= 2) {
                pkt.id0 = node.parents[0];
                pkt.id1 = node.parents[1];
            }
            break;
        }

        case GeoType::POINT_CONSTRAINED: {
            pkt.op = (uint8_t)OpCode::CREATE_CONSTRAINED_POINT;
            // 目标对象 ID（吸附在哪个物体上）存在结果槽 i0 中
            pkt.id0 = static_cast<uint32_t>(node.result.i0);

            // 💡 约束点的锚点位置公式同样嵌入在自己的通道 0 和 1 中
            pkt.s0 = node.channels[0].original_infix; // 锚点 X 源码
            pkt.s1 = node.channels[1].original_infix; // 锚点 Y 源码
            break;
        }

        case GeoType::SCALAR_INTERNAL: {
            // 独立标量节点的公式存储在自己的通道 0 中
            pkt.op = (uint8_t)OpCode::CREATE_INTERNAL_SCALAR;
            pkt.s0 = node.channels[0].original_infix;
            break;
        }

        default:
            // 可以在此继续扩展其他几何类型
            break;
    }
    return pkt;
}

    static std::vector<CommandPacket> TakeSnapshot(const GeometryGraph& graph) {
        std::vector<const GeoNode*> master_nodes;
        for (const auto& node : graph.node_pool) {
            if (node.active && !GeoType::is_scalar(node.type)) {
                master_nodes.push_back(&node);
            }
        }
        std::ranges::sort(master_nodes, [](auto a, auto b) { return a->id < b->id; });

        std::vector<CommandPacket> recipe;
        recipe.reserve(master_nodes.size());
        for (auto n : master_nodes) {
            recipe.push_back(ExtractNodeToCommand(*n, graph));
        }
        return recipe;
    }

    // --- 核心逻辑：执行、提交、检出 ---

    static void ExecuteSingle(GeometryGraph &graph, const CommandPacket &pkt) {
        GeoNode::VisualConfig cfg;
        cfg.name = pkt.name; // 💡 恢复时保持名字一致
        cfg.thickness = pkt.thickness; cfg.color = pkt.color;
        cfg.is_visible = pkt.is_visible; cfg.show_label = pkt.show_label;
        cfg.label_offset_x = pkt.label_offset_x; cfg.label_offset_y = pkt.label_offset_y;
        cfg.label_size = pkt.label_size; cfg.label_color = pkt.label_color;

        switch (static_cast<OpCode>(pkt.op)) {
            case OpCode::CREATE_FREE_POINT:
                GeoFactory::AddFreePoint(graph, pkt.s0, pkt.s1, cfg); break;
            case OpCode::CREATE_SEGMENT_2P:
                GeoFactory::AddSegment(graph, pkt.id0, pkt.id1, cfg); break;
            case OpCode::CREATE_MID_POINT:
                GeoFactory::AddMidPoint(graph, pkt.id0, pkt.id1, cfg); break;
            case OpCode::CREATE_CONSTRAINED_POINT:
                GeoFactory::AddConstrainedPoint(graph, pkt.id0, pkt.s0, pkt.s1, cfg); break;
            case OpCode::DELETE_PHYSICAL:
                GeoFactory::DeleteObjectRecursive(graph, pkt.id0); break;
            case OpCode::UPDATE_POINT_SCALAR:
                GeoFactory::UpdatePointScalar(graph, pkt.id0, pkt.s0, pkt.s1); break;
            case OpCode::UPDATE_SCALAR:
                GeoFactory::InternalUpdateScalar(graph, pkt.id0, pkt.s0); break;
            case OpCode::UPDATE_VIEW_TRANSFORM:
                GeoFactory::UpdateViewTransform(graph, pkt.d0, pkt.d1, pkt.d2); break;
            case OpCode::UPDATE_VIEW_SIZE:
                GeoFactory::UpdateViewSize(graph, pkt.d0, pkt.d1); break;
            default: break;
        }
    }

    // --- src/graph/GeoCommands.cpp ---

    void Execute(GeometryGraph &graph, std::vector<CommandPacket> &bus) {
        if (bus.empty()) return;

        // 💡 状态标记：默认本次任务不改变几何结构
        bool has_geometry_change = false;

        // 1. 遍历并执行指令
        for (const auto &pkt : bus) {
            OpCode op = static_cast<OpCode>(pkt.op);

            // 判断是否为几何变更（创建、删除、修改公式等）
            // 排除视图更新：UPDATE_VIEW_TRANSFORM (0x80) 和 UPDATE_VIEW_SIZE (0x81)
            if (op != OpCode::UPDATE_VIEW_TRANSFORM && op != OpCode::UPDATE_VIEW_SIZE) {
                has_geometry_change = true;
            }

            ExecuteSingle(graph, pkt);
        }

        // 2. 核心计算（无论视图还是几何变了，都需要重新采样/投影）
        calculate_points_core(graph);

        // 3. 💡 Git 分支逻辑：仅在几何结构发生实质性变化时触发
        if (has_geometry_change) {
            HistoryNode newNode;
            newNode.id = graph.version_id_counter++;
            newNode.parent_id = graph.head_version_id;

            // 拍照：只保存几何对象的“配方”
            newNode.recipe = TakeSnapshot(graph);

            // 建立父子链接（产生分支）
            if (graph.head_version_id != -1) {
                for (auto& node : graph.history_tree) {
                    if ((int32_t)node.id == graph.head_version_id) {
                        node.children.push_back(newNode.id);
                        break;
                    }
                }
            }

            graph.history_tree.push_back(newNode);
            graph.head_version_id = (int32_t)newNode.id;

            // 调试信息（可选）
            // std::cout << "[Git] New Commit: " << newNode.id << " (Parent: " << newNode.parent_id << ")" << std::endl;
        }

        bus.clear();
    }

    void CheckoutVersion(GeometryGraph& graph, uint32_t target_id) {
        auto it = std::ranges::find_if(graph.history_tree,
                                       [target_id](const HistoryNode& n){ return n.id == target_id; });
        if (it == graph.history_tree.end()) return;

        // 1. 彻底清空当前世界
        graph.ClearEverything();

        // 2. 按配方重演
        for (const auto& pkt : it->recipe) {
            ExecuteSingle(graph, pkt);
        }

        calculate_points_core(graph);
        graph.head_version_id = (int32_t)target_id;
    }

    void Undo(GeometryGraph& graph) {
        if (graph.head_version_id == -1) return;

        // 查找当前 HEAD 节点的父节点
        for (const auto& node : graph.history_tree) {
            if ((int32_t)node.id == graph.head_version_id) {
                if (node.parent_id != -1) {
                    CheckoutVersion(graph, (uint32_t)node.parent_id);
                } else {
                    // 如果撤销到最初状态
                    graph.ClearEverything();
                    calculate_points_core(graph);
                    graph.head_version_id = -1;
                }
                break;
            }
        }
    }

    void Redo(GeometryGraph& graph) {
        // 在分支结构中，Redo 默认沿着最后一个（最新创建的）孩子前进
        if (graph.head_version_id == -1) {
            if (!graph.history_tree.empty()) CheckoutVersion(graph, graph.history_tree[0].id);
            return;
        }

        for (const auto& node : graph.history_tree) {
            if ((int32_t)node.id == graph.head_version_id) {
                if (!node.children.empty()) {
                    CheckoutVersion(graph, node.children.back());
                }
                break;
            }
        }
    }

} // namespace GeoCommand

// --- EMSCRIPTEN 绑定保持不变，但在 JS 中通过 Execute(graphInstance, bus) 调用 ---

// =========================================================
#ifdef __EMSCRIPTEN__
using namespace emscripten;
extern GeometryGraph g_mainGraph;

void JS_Execute(const std::vector<GeoCommand::CommandPacket> &bus) {
    GeoCommand::Execute(g_mainGraph, bus);
}

EMSCRIPTEN_BINDINGS (geo_bus_1d_module) {
    value_object<GeoCommand::CommandPacket>("CommandPacket")
            .field("op", &GeoCommand::CommandPacket::op)
            .field("id0", &GeoCommand::CommandPacket::id0)
            .field("id1", &GeoCommand::CommandPacket::id1)
            .field("id2", &GeoCommand::CommandPacket::id2)
            .field("d0", &GeoCommand::CommandPacket::d0)
            .field("d1", &GeoCommand::CommandPacket::d1)
            .field("d2", &GeoCommand::CommandPacket::d2)
            .field("s0", &GeoCommand::CommandPacket::s0)
            .field("s1", &GeoCommand::CommandPacket::s1)
            .field("thickness", &GeoCommand::CommandPacket::thickness)
            .field("color", &GeoCommand::CommandPacket::color)
            .field("is_visible", &GeoCommand::CommandPacket::is_visible);

    // 💡 只需要注册一个一维向量
    register_vector<GeoCommand::CommandPacket>("CommandBus");

    function("execute", &JS_Execute);
}
#endif
