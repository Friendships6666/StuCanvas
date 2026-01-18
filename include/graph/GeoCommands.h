// --- include/graph/GeoCommands.h ---
#ifndef GEO_COMMANDS_H
#define GEO_COMMANDS_H

#include "GeoGraph.h"
#include <string>
#include <vector>

namespace GeoCommand {

    enum class OpCode : uint8_t {
        NONE = 0,
        CREATE_INTERNAL_SCALAR   = 0x10,
        CREATE_FREE_POINT        = 0x20,
        CREATE_SEGMENT_2P        = 0x30,
        CREATE_MID_POINT         = 0x40,
        CREATE_CONSTRAINED_POINT = 0x50,
        DELETE_PHYSICAL          = 0x60,
        UPDATE_POINT_SCALAR      = 0x70,
        UPDATE_VIEW_TRANSFORM    = 0x80,
        UPDATE_VIEW_SIZE         = 0x81
    };

    /**
     * @brief 大一统扁平指令包 (1D 单元)
     * 每个包就是一个完整的函数调用请求
     */
    struct CommandPacket {
        uint8_t  op = 0;
        uint32_t id0 = 0xFFFFFFFF; // 💡 统一使用 id0 作为 target_id 或第一个 ID 参数
        uint32_t id1 = 0xFFFFFFFF;
        uint32_t id2 = 0xFFFFFFFF;

        double   d0 = 0.0, d1 = 0.0, d2 = 0.0;
        std::string s0, s1;

        float    thickness = 2.0f;
        uint32_t color = 0x4D4DFFFF;
        bool     is_visible = true;
        bool     show_label = true;

        CommandPacket() = default;
        explicit CommandPacket(OpCode o) : op(static_cast<uint8_t>(o)) {}
    };

    /**
     * @brief 唯一执行入口 (一维数组)
     * 执行完数组内所有指令后，自动触发计算核心
     */
    void Execute(GeometryGraph& graph, std::vector<CommandPacket>& bus);

}

#endif