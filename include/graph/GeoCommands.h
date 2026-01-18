// --- 文件路径: include/graph/GeoCommands.h ---
#ifndef GEO_COMMANDS_H
#define GEO_COMMANDS_H

#include "GeoGraph.h"
#include <string>
#include <vector>
#include <cstdint>

namespace GeoCommand {

    enum class OpCode : uint8_t {
        NONE = 0,
        CREATE_INTERNAL_SCALAR = 0x10,
        CREATE_FREE_POINT      = 0x20,
        CREATE_SEGMENT_2P      = 0x30,
        CREATE_MID_POINT       = 0x40,
        CREATE_CONSTRAINED_POINT = 0x50,
        DELETE_PHYSICAL        = 0x60, // 参数: 使用 target_id 指定要删除的根对象
        UPDATE_FORMULA         = 0x70
    };

    struct CommandPacket {
        uint8_t  op = 0;
        uint32_t target_id = 0xFFFFFFFF; // 对于 DELETE_PHYSICAL，这是起始删除 ID

        uint32_t p0 = 0xFFFFFFFF;
        uint32_t p1 = 0xFFFFFFFF;
        uint32_t p2 = 0xFFFFFFFF;

        std::string s0;
        std::string s1;

        float    thickness = 2.0f;
        uint32_t color = 0x4D4DFFFF;
        bool     is_visible = true;
        bool     show_label = true;

        CommandPacket() = default;
        explicit CommandPacket(OpCode o) : op(static_cast<uint8_t>(o)) {}
    };

    uint32_t Execute(GeometryGraph& graph, const CommandPacket& pkt);
    void ExecuteBatch(GeometryGraph& graph, const std::vector<CommandPacket>& packets);

}

#endif