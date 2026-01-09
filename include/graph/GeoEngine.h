// --- 文件路径: include/graph/GeoEngine.h ---
#ifndef GEO_ENGINE_H
#define GEO_ENGINE_H

#include "GeoGraph.h"
#include "CommandManager.h"
#include "GeoFactory.h"
#include "../plot/plotCall.h" // 注意跨目录引用
#include <vector>

class GeoEngine {
public:
    // 核心组件开放，允许高级用户直接访问
    GeometryGraph graph;
    CommandManager cmdManager;
    ViewState view;
    
    // 维护绘制顺序（对应图层列表）
    std::vector<uint32_t> draw_order;

    /**
     * @brief 初始化引擎与视图
     * @param width 屏幕像素宽度
     * @param height 屏幕像素高度
     */
    GeoEngine(double width, double height) : cmdManager(graph) {
        view.screen_width = width;
        view.screen_height = height;
        view.zoom = 0.1; // 默认缩放
        
        // 计算初始 WPP (World Per Pixel)
        double aspect = width / height;
        view.wppx = (2.0 * aspect) / (view.zoom * width);
        view.wppy = -2.0 / (view.zoom * height);
        view.offset_x = 0.0; 
        view.offset_y = 0.0;
        
        UpdateWorldOrigin();
    }

    // =========================================================
    // 1. 创建接口 (一键提交事务)
    // =========================================================

    uint32_t AddPoint(double x, double y) {
        // RPNParam 支持表达式，这里简化为纯数值传入
        auto tx = GeoFactory::CreatePoint(graph, {x}, {y});
        uint32_t id = tx.main_id;
        SubmitAndRegister(std::move(tx), id);
        return id;
    }

    uint32_t AddLine(uint32_t p1_id, uint32_t p2_id, bool is_infinite = false) {
        auto tx = GeoFactory::CreateLine(graph, p1_id, p2_id, is_infinite);
        uint32_t id = tx.main_id;
        SubmitAndRegister(std::move(tx), id);
        return id;
    }

    uint32_t AddCircle(uint32_t center_id, double radius) {
        auto tx = GeoFactory::CreateCircle(graph, center_id, {radius});
        uint32_t id = tx.main_id;
        SubmitAndRegister(std::move(tx), id);
        return id;
    }
    
    uint32_t AddCircle3P(uint32_t p1, uint32_t p2, uint32_t p3) {
        auto tx = GeoFactory::CreateCircleThreePoints(graph, p1, p2, p3);
        uint32_t id = tx.main_id;
        SubmitAndRegister(std::move(tx), id);
        return id;
    }

    // =========================================================
    // 2. 交互更新接口
    // =========================================================

    void MovePoint(uint32_t id, double x, double y) {
        // 生成移动事务并提交
        auto tx = GeoFactory::UpdateFreePoint_Tx(graph, id, {x}, {y});
        // 注意：移动操作不改变 draw_order，只改变数据
        cmdManager.Submit(std::move(tx));
    }

    void UpdateStyle(uint32_t id, const GeoNode::VisualConfig& new_style) {
        auto tx = GeoFactory::UpdateStyle_Tx(graph, id, new_style);
        cmdManager.Submit(std::move(tx));
    }

    void DeleteObject(uint32_t id) {
        auto tx = GeoFactory::DeleteObject_Tx(graph, id);
        cmdManager.Submit(std::move(tx));
        // 注意：这里我们不从 draw_order 移除 ID，而是依赖 node.active = false
        // 渲染器会自动跳过不活跃节点，这样撤销时 ID 位置也能保留
    }

    // =========================================================
    // 3. 视图操作接口
    // =========================================================

    void PanZoom(double new_offset_x, double new_offset_y, double new_zoom) {
        ViewState new_view = view;
        new_view.offset_x = new_offset_x;
        new_view.offset_y = new_offset_y;
        new_view.zoom = new_zoom;
        
        // 重新计算 WPP 和 Origin
        double aspect = view.screen_width / view.screen_height;
        new_view.wppx = (2.0 * aspect) / (new_view.zoom * view.screen_width);
        new_view.wppy = -2.0 / (new_view.zoom * view.screen_height);
        
        // 更新 Origin 逻辑
        new_view.world_origin.x = new_view.offset_x - (view.screen_width * 0.5) * new_view.wppx;
        new_view.world_origin.y = new_view.offset_y - (view.screen_height * 0.5) * new_view.wppy;

        // 手动构建视图事务
        Transaction tx;
        tx.description = "Viewport Change";
        tx.is_viewport_transaction = true;
        
        // 记录旧视图与新视图用于 Undo/Redo
        // 这里的 ID 0 是虚构的，用于占位，实际上 ApplyTransaction 会更新全局 g_global_view_state
        tx.mutations.push_back({ 
            Mutation::Type::VIEWPORT, 
            0,        // node_id (视图更新不针对特定节点，给 0 即可)
            view,     // old_val (自动匹配 variant 中的 ViewState 类型)
            new_view  // new_val (自动匹配 variant 中的 ViewState 类型)
        });


        cmdManager.Submit(std::move(tx));
        
        // 本地状态同步（虽然 Commit 也会更新，但保持 Engine 状态最新是好习惯）
        // 实际上 Commit 内部会处理，这里为了逻辑严谨，可以等待 Commit 后更新
        // 或者因为是预测执行，这里先不改，等 Commit 回调
    }

    // =========================================================
    // 4. 历史记录控制
    // =========================================================

    void Undo() { cmdManager.Undo(); }
    void Redo() { cmdManager.Redo(); }

    // =========================================================
    // 5. 核心驱动：计算与渲染
    // =========================================================

    /**
     * @brief 执行一帧的计算与渲染
     * 如果队列中有 Viewport 事务，执行全量重绘；
     * 否则执行增量追加渲染。
     */
    void Render() {
        // view 会在 Commit 内部被更新为最新状态
        cmdManager.Commit(view, draw_order);
    }

private:
    void UpdateWorldOrigin() {
        view.world_origin.x = view.offset_x - (view.screen_width * 0.5) * view.wppx;
        view.world_origin.y = view.offset_y - (view.screen_height * 0.5) * view.wppy;
    }

    void SubmitAndRegister(Transaction tx, uint32_t id) {
        cmdManager.Submit(std::move(tx));
        draw_order.push_back(id); // 将新对象加入绘制列表
    }
};

#endif // GEO_ENGINE_H