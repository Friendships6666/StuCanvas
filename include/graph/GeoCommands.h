// --- include/graph/GeoCommands.h ---
#ifndef GEO_COMMANDS_H
#define GEO_COMMANDS_H

class GeometryGraph;
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
        UPDATE_SCALAR            = 0x71, // 💡 新增：更新纯标量公式
        UPDATE_VIEW_TRANSFORM    = 0x80,
        UPDATE_VIEW_SIZE         = 0x81
    };


    struct CommandPacket {
        uint8_t  op = 0;
        uint32_t id0 = NULL_ID; // 目标ID或第一个ID参数
        uint32_t id1 = NULL_ID;
        uint32_t id2 = NULL_ID;

        double   d0 = 0.0, d1 = 0.0, d2 = 0.0;
        std::string s0, s1; // 存储公式 expr0, expr1

        // --- 极致视觉属性填充 (与 VisualConfig 一一对应) ---
        std::string name;
        float    thickness = 2.0f;
        uint32_t color = 0x4D4DFFFF;
        bool     is_visible = true;
        bool     show_label = true;
        float    label_offset_x = 15.0f;
        float    label_offset_y = -15.0f;
        float    label_size = 12.0f;
        uint32_t label_color = 0x4D4DFFFF;

        // --- 状态掩码备份 ---
        uint64_t state_mask = 0;

        CommandPacket() = default;
        explicit CommandPacket(OpCode o) : op(static_cast<uint8_t>(o)) {}
    };



    /**
     * @brief 唯一执行入口 (一维数组)
     * 执行完数组内所有指令后，自动触发计算核心
     */
    void Execute(GeometryGraph& graph, std::vector<CommandPacket>& bus);
    void Undo(GeometryGraph& graph);
    void Redo(GeometryGraph& graph);
    void CheckoutVersion(GeometryGraph& graph, uint32_t target_id);

}

#endif