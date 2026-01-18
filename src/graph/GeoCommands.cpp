// --- 文件路径: src/graph/GeoCommands.cpp ---
#include "../../include/graph/GeoCommands.h"
#include "../../include/graph/GeoFactory.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#endif

namespace GeoCommand {

uint32_t Execute(GeometryGraph& graph, const CommandPacket& pkt) {
    // 准备视觉配置
    GeoNode::VisualConfig cfg;
    cfg.thickness = pkt.thickness;
    cfg.color = pkt.color;
    cfg.is_visible = pkt.is_visible;
    cfg.show_label = pkt.show_label;

    switch (static_cast<OpCode>(pkt.op)) {

        case OpCode::CREATE_INTERNAL_SCALAR:
            return GeoFactory::AddInternalScalar(graph, pkt.s0, cfg);

        case OpCode::CREATE_FREE_POINT:
            return GeoFactory::AddFreePoint(graph, pkt.s0, pkt.s1, cfg);

        case OpCode::CREATE_SEGMENT_2P:
            return GeoFactory::AddSegment(graph, pkt.p0, pkt.p1, cfg);

        case OpCode::CREATE_MID_POINT:
            return GeoFactory::AddMidPoint(graph, pkt.p0, pkt.p1, cfg);

        case OpCode::CREATE_CONSTRAINED_POINT:
            return GeoFactory::AddConstrainedPoint(graph, pkt.p0, pkt.s0, pkt.s1, cfg);

        // =========================================================
        // 核心变更：物理删除逻辑
        // =========================================================
        case OpCode::DELETE_PHYSICAL:
            // 调用工厂中的递归删除：删除自己及所有后代，释放堆内存，重整 LUT 映射
            GeoFactory::DeleteObjectRecursive(graph, pkt.target_id);
            return 0; // 删除操作无返回值

        case OpCode::UPDATE_FORMULA:
            // 假设后续实现此函数
            // return GeoFactory::UpdateFormula(graph, pkt.target_id, pkt.s0);
            return 0;

        default:
            return 0xFFFFFFFF;
    }
}

void ExecuteBatch(GeometryGraph& graph, const std::vector<CommandPacket>& packets) {
    for (const auto& pkt : packets) {
        // 由于 DeleteObjectRecursive 会导致向量位移
        // 但我们的 Execute 内部总是通过 ID 重新 GetNode，所以 Batch 是安全的
        Execute(graph, pkt);
    }
}

} // namespace GeoCommand

// =========================================================
// WASM 绑定区 (宏包围)
// =========================================================
#ifdef __EMSCRIPTEN__
using namespace emscripten;

extern GeometryGraph g_mainGraph;

void JS_ExecuteBatch(const std::vector<GeoCommand::CommandPacket>& packets) {
    GeoCommand::ExecuteBatch(g_mainGraph, packets);
}

uint32_t JS_ExecuteSingle(const GeoCommand::CommandPacket& pkt) {
    return GeoCommand::Execute(g_mainGraph, pkt);
}

EMSCRIPTEN_BINDINGS(geo_commands_module) {

    value_object<GeoCommand::CommandPacket>("CommandPacket")
        .field("op",        &GeoCommand::CommandPacket::op)
        .field("target_id", &GeoCommand::CommandPacket::target_id)
        .field("p0",        &GeoCommand::CommandPacket::p0)
        .field("p1",        &GeoCommand::CommandPacket::p1)
        .field("p2",        &GeoCommand::CommandPacket::p2)
        .field("s0",        &GeoCommand::CommandPacket::s0)
        .field("s1",        &GeoCommand::CommandPacket::s1)
        .field("thickness", &GeoCommand::CommandPacket::thickness)
        .field("color",     &GeoCommand::CommandPacket::color)
        .field("is_visible",&GeoCommand::CommandPacket::is_visible)
        .field("show_label",&GeoCommand::CommandPacket::show_label);

    register_vector<GeoCommand::CommandPacket>("CommandPacketVector");

    function("execute", &JS_ExecuteSingle);
    function("executeBatch", &JS_ExecuteBatch);
}
#endif