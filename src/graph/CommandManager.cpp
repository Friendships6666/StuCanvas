#include "../../include/graph/CommandManager.h"
#include "../../include/plot/plotCall.h"
#include "../../include/graph/GeoFactory.h"

// 修改后的 Submit 接口
void CommandManager::Submit(Transaction tx) {
    m_pending_queue.push_back({ std::move(tx), false });
    m_redo_stack.clear(); // ★ 核心：新动作发生，清空重做链
}

void CommandManager::Undo() {
    if (m_undo_stack.empty()) return;
    Transaction tx = std::move(m_undo_stack.back());
    m_undo_stack.pop_back();

    // 撤销动作本质是产生一个逆向 Transaction 进入 pending 队列
    // 标记为特殊类型以防止它再次进入 undo 栈（循环）
    m_pending_queue.push_back({ "UNDO: " + tx.description, tx.mutations, tx.is_viewport_transaction });
    // 将其放入 redo 栈
    m_redo_stack.push_back(tx); 
}

void CommandManager::Redo() {
    if (m_redo_stack.empty()) return;

    Transaction tx = std::move(m_redo_stack.back());
    m_redo_stack.pop_back();

    // 重新压回 Undo 栈
    m_undo_stack.push_back(tx);

    // 正常执行 (is_undo = false)
    m_pending_queue.push_back({ tx, false });
}

void CommandManager::Commit(ViewState& current_view, const std::vector<uint32_t>& draw_order) {
    if (m_pending_queue.empty()) return;

    std::unordered_set<uint32_t> dirty_nodes;
    bool needs_viewport_refresh = false;

    // 1. 解释执行阶段
    while (!m_pending_queue.empty()) {
        PendingTask task = std::move(m_pending_queue.front());
        m_pending_queue.pop_front();

        // 检查是否包含视图变更
        if (task.tx.is_viewport_transaction) {
            needs_viewport_refresh = true;

            // ★ 逻辑完善：从 Mutation 中提取最新的 ViewState 更新本地变量
            // 假设 Transaction 约定：如果是 Viewport 类型的，第一个 Mutation 存的就是 ViewState
            for(const auto& m : task.tx.mutations) {
                 if (m.type == Mutation::Type::VIEWPORT) {
                     // 无论 Undo 还是 Redo，取对应的值覆盖 current_view
                     const auto& val = task.is_undo_op ? m.old_val : m.new_val;
                     current_view = std::get<ViewState>(val);
                 }
            }
        }

        // 执行物理修改
        ApplyTransaction(task.tx, task.is_undo_op, dirty_nodes);
    }

    // 2. 策略执行阶段
    if (needs_viewport_refresh) {
        // --- 视图模式 ---
        // 动作：清空 Buffer，全员重采样
        calculate_points_core(
            wasm_final_contiguous_buffer,
            wasm_function_ranges_buffer,
            m_graph,        // ★ 修正：传 graph 对象，而非 node_pool
            draw_order,
            {},             // 视图模式不需要脏列表
            current_view,   // ★ 此时 current_view 已是最新
            RenderUpdateMode::Viewport
        );
    } else {
        // --- 增量模式 ---
        if (!dirty_nodes.empty()) {
            std::vector<uint32_t> moved_list(dirty_nodes.begin(), dirty_nodes.end());

            // 利用位图跳跃扫描，找出所有受灾后代
            std::vector<uint32_t> affected_targets = m_graph.FastScan(moved_list);

            // 动作：保留 Buffer，追加新数据
            calculate_points_core(
                wasm_final_contiguous_buffer,
                wasm_function_ranges_buffer,
                m_graph,        // ★ 修正：传 graph 对象
                draw_order,
                affected_targets,
                current_view,
                RenderUpdateMode::Incremental
            );
        }
    }
}

void CommandManager::ApplyTransaction(const Transaction& tx, bool is_undo, std::unordered_set<uint32_t>& dirty_set) {
    // 处理 Undo 时的逆序执行逻辑
    if (is_undo) {
        for (auto it = tx.mutations.rbegin(); it != tx.mutations.rend(); ++it) {
            ExecuteSingleMutation(*it, true, dirty_set);
        }
    } else {
        for (auto it = tx.mutations.begin(); it != tx.mutations.end(); ++it) {
            ExecuteSingleMutation(*it, false, dirty_set);
        }
    }
}

// 内部辅助：真正的物理修改发生地
void CommandManager::ExecuteSingleMutation(const Mutation& m, bool is_undo, std::unordered_set<uint32_t>& dirty_set) {
    auto& node = m_graph.node_pool[m.node_id];
    const auto& val = is_undo ? m.old_val : m.new_val;

    switch (m.type) {
        case Mutation::Type::ACTIVE: {
            bool active = std::get<bool>(val);
            if (node.active != active) {
                node.active = active;
                // 逻辑开关：活跃则入桶，不活跃则彻底断开链表
                if (node.active) {
                    m_graph.MoveNodeInBuckets(node.id, node.rank);
                } else {
                    m_graph.DetachFromBucket(node.id);
                }
            }
            break;
        }

        case Mutation::Type::DATA:
            node.data = std::get<GeoNode::GeoPayload>(val);
            break;

        case Mutation::Type::STYLE:
            node.config = std::get<GeoNode::VisualConfig>(val);
            break;

        case Mutation::Type::LINKS: {
            // 这里会触发 Rank 递归搬家，MoveNodeInBuckets 会在内部被调用
            auto pids = std::get<std::vector<uint32_t>>(val);
            m_graph.LinkAndRank(node.id, pids);
            break;
        }

        case Mutation::Type::VIEWPORT:
            // 视口更新通常是全局单例，直接覆盖
            g_global_view_state = std::get<ViewState>(val);
            break;
    }

    // 只要不是纯视口变化，就需要记录受灾节点
    if (m.type != Mutation::Type::VIEWPORT) {
        dirty_set.insert(m.node_id);
    }
}