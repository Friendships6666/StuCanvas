// --- src/graph/GeoCommands.cpp ---
#include "../../include/graph/GeoCommands.h"
#include "../../include/graph/GeoFactory.h"
#include "../../include/plot/plotCall.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#endif

extern std::vector<PointData> wasm_final_contiguous_buffer;
extern std::vector<FunctionRange> wasm_function_ranges_buffer;

namespace GeoCommand {

// 内部逻辑：分发单个包
static void ExecuteSingle(GeometryGraph& graph, const CommandPacket& pkt) {
    GeoNode::VisualConfig cfg;
    cfg.thickness = pkt.thickness; cfg.color = pkt.color;
    cfg.is_visible = pkt.is_visible; cfg.show_label = pkt.show_label;

    switch (static_cast<OpCode>(pkt.op)) {
        case OpCode::CREATE_INTERNAL_SCALAR:
            GeoFactory::AddInternalScalar(graph, pkt.s0, cfg); break;
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
        case OpCode::UPDATE_VIEW_TRANSFORM:
            GeoFactory::UpdateViewTransform(graph, pkt.d0, pkt.d1, pkt.d2); break;
        case OpCode::UPDATE_VIEW_SIZE:
            GeoFactory::UpdateViewSize(graph, pkt.d0, pkt.d1); break;
        default: break;
    }
}

void Execute(GeometryGraph& graph, std::vector<CommandPacket>& bus) {
    if (bus.empty()) return;

    // 1. 线性处理所有函数调用包
    for (const auto& pkt : bus) {
        ExecuteSingle(graph, pkt);
    }

    // 2. 💡 上级领导最后下达“总攻”命令
    // 执行全图同步、解算、采样、绘制
    calculate_points_core(
        wasm_final_contiguous_buffer,
        wasm_function_ranges_buffer,
        graph
    );
    bus.clear();
}

} // namespace GeoCommand

// =========================================================
#ifdef __EMSCRIPTEN__
using namespace emscripten;
extern GeometryGraph g_mainGraph;

void JS_Execute(const std::vector<GeoCommand::CommandPacket>& bus) {
    GeoCommand::Execute(g_mainGraph, bus);
}

EMSCRIPTEN_BINDINGS(geo_bus_1d_module) {
    value_object<GeoCommand::CommandPacket>("CommandPacket")
        .field("op",        &GeoCommand::CommandPacket::op)
        .field("id0",       &GeoCommand::CommandPacket::id0)
        .field("id1",       &GeoCommand::CommandPacket::id1)
        .field("id2",       &GeoCommand::CommandPacket::id2)
        .field("d0",        &GeoCommand::CommandPacket::d0)
        .field("d1",        &GeoCommand::CommandPacket::d1)
        .field("d2",        &GeoCommand::CommandPacket::d2)
        .field("s0",        &GeoCommand::CommandPacket::s0)
        .field("s1",        &GeoCommand::CommandPacket::s1)
        .field("thickness", &GeoCommand::CommandPacket::thickness)
        .field("color",     &GeoCommand::CommandPacket::color)
        .field("is_visible",&GeoCommand::CommandPacket::is_visible);

    // 💡 只需要注册一个一维向量
    register_vector<GeoCommand::CommandPacket>("CommandBus");

    function("execute", &JS_Execute);
}
#endif