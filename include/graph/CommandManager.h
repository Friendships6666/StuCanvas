#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include "GeoGraph.h"
#include <deque>
#include <stack>
#include "../graph/GeoGraph.h"

/**
 * @brief 原子突变：最底层的物理属性变更
 */
struct Mutation {
    enum class Type {
        ACTIVE,     // 增/删 (bool)
        DATA,       // 坐标/公式 (GeoPayload)
        STYLE,      // 颜色/粗细 (VisualConfig)
        LINKS,      // 拓扑父节点 (std::vector<uint32_t>)
        VIEWPORT    // 视口状态 (ViewState)
    } type;

    uint32_t node_id;

    // 使用 variant 存储旧值和新值，支持完全撤销
    using Value = std::variant<std::monostate, bool, GeoNode::GeoPayload, GeoNode::VisualConfig, std::vector<uint32_t>, ViewState>;
    Value old_val;
    Value new_val;
};

/**
 * @brief 事务：用户的一个逻辑步骤（如“删除圆”，涉及圆和其关联标签的多个 Mutation）
 */
struct Transaction {
    std::string description;
    std::vector<Mutation> mutations;
    bool is_viewport_transaction = false;
    uint32_t main_id = 0xFFFFFFFF;
};

class CommandManager {
public:
    CommandManager(GeometryGraph& graph) : m_graph(graph) {}

    // --- 外部接口：6 大动作提交 ---
    void Submit(Transaction tx);
    void Undo();
    void Redo();

    // --- 核心调度：提交并计算 ---
    void CommandManager::Commit(ViewState& current_view, const std::vector<uint32_t>& draw_order);
    void CommandManager::ExecuteSingleMutation(const Mutation& m, bool is_undo, std::unordered_set<uint32_t>& dirty_set);
    struct PendingTask {
        Transaction tx;
        bool is_undo_op = false; // true 表示执行 Undo 逻辑，false 表示执行正常/Redo 逻辑
    };

private:

    GeometryGraph& m_graph;
    std::deque<PendingTask> m_pending_queue; // 待处理队列
    std::vector<Transaction> m_undo_stack;   // 历史栈
    std::vector<Transaction> m_redo_stack;   // 重做栈

    // 内部执行引擎
    void ApplyTransaction(const Transaction& tx, bool is_undo, std::unordered_set<uint32_t>& dirty_set);
};

#endif