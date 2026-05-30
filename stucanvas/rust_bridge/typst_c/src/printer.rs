// src/printer.rs
/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

use crate::types::*;

pub unsafe fn print_detailed_outline(outline_ptr: *const Outline) {
    if outline_ptr.is_null() {
        println!("❌ [Printer] 传入的 Outline 指针为空！");
        return;
    }
    unsafe {
        print_detailed_outline_impl(outline_ptr, "");
    }
}

unsafe fn print_detailed_outline_impl(outline_ptr: *const Outline, indent: &str) {
    let outline: &Outline = &*outline_ptr;
    let geometries = outline.geometries.as_slice();
    let instances = outline.instances.as_slice();

    println!("{}--------------------------------------------------------------------", indent);
    println!("{}▲ [Instanced Outline] 地址: {:?}, 共享几何体数: {}, 实例化指令数: {}",
             indent, outline_ptr, geometries.len(), instances.len());
    println!("{}--------------------------------------------------------------------", indent);

    // 1. 打印去重几何池
    println!("{}  === 1. 共享几何体池 (Shared Geometry Pool) ===", indent);
    for shared_geom in geometries {
        println!("{}    * [Geometry ID: {}] 物理偏移: +{} bytes",
                 indent, shared_geom.geometry_id, shared_geom.geometry_id as usize * std::mem::size_of::<SharedGeometry>());
        print_geometry_details(&shared_geom.geometry, indent);
    }

    // 2. 打印绘制指令流
    println!("\n{}  === 2. 实例化绘制指令 (Draw Instances) ===", indent);
    for (idx, instance) in instances.iter().enumerate() {
        println!("{}    * [Instance {}] 引用 Geometry ID: {} | 内存偏移: +{} bytes",
                 indent, idx, instance.geometry_id, idx * std::mem::size_of::<DrawInstance>());
        println!("{}      - 全局不透明度 (Opacity): {:.4}", indent, instance.opacity);
        println!("{}      - 启用裁剪 (Clip Mask): {}", indent, instance.clip);

        let ts = &instance.transform;
        println!("{}      - 局部 2D 仿射变换矩阵 (Transform2D - Column-Major):", indent);
        println!(
            "{}        * Row 0 [sx, kx, tx]: | sx: {:<9.4}% | kx: {:<9.4}% | tx: {:.4} pt",
            indent, ts.sx * 100.0, ts.kx * 100.0, ts.tx
        );
        println!(
            "{}        * Row 1 [ky, sy, ty]: | ky: {:<9.4}% | sy: {:<9.4}% | ty: {:.4} pt",
            indent, ts.ky * 100.0, ts.sy * 100.0, ts.ty
        );
        // 材质与描边
        println!("{}      - 填充规则 (Fill Rule): {:?}", indent, instance.fill_rule);
        print_paint_details(&instance.fill_paint, "填充 (Fill)", indent);

        if instance.stroke_width > 0.0 {
            println!("{}      - 描边厚度 (Stroke Width): {:.4} pt", indent, instance.stroke_width);
            println!("{}      - 端点样式 (Line Cap): {:?}", indent, instance.stroke_cap);
            println!("{}      - 拐角样式 (Line Join): {:?}", indent, instance.stroke_join);
            println!("{}      - 斜角限制 (Miter Limit): {:.4}", indent, instance.miter_limit);

            let dashes = instance.dash_array.as_slice();
            if dashes.is_empty() {
                println!("{}      - 虚线步长 (Dash Array): 无 (Solid Line)", indent);
            } else {
                print!("{}      - 虚线步长 (Dash Array): [", indent);
                for (d_idx, d) in dashes.iter().enumerate() {
                    if d_idx > 0 { print!(", "); }
                    print!("{:.2}pt", d);
                }
                println!("], 偏移(Offset)={:.2}pt", instance.dash_offset);
            }
            print_paint_details(&instance.stroke_paint, "描边 (Stroke)", indent);
        } else {
            println!("{}      - 描边属性: 无描边 (None / stroke_width == 0)", indent);
        }
        println!("{}    ====================================================================", indent);
    }
}

unsafe fn print_geometry_details(geom: &Geometry, indent: &str) {
    match geom.ty {
        GeometryType::Line => {
            let line = &geom.data.line;
            println!("{}      - 几何类型: [Line (直线段)]", indent);
            println!("{}        * 起点 (Start): ({:.4}, {:.4}) pt", indent, line.start.x, line.start.y);
            println!("{}        * 终点 (End)  : ({:.4}, {:.4}) pt", indent, line.end.x, line.end.y);
        }
        GeometryType::Rect => {
            let rect = &geom.data.rect;
            println!("{}      - 几何类型: [Rect (矩形与圆角矩形)]", indent);
            println!("{}        * 物理尺寸: 宽={:.4} pt, 高={:.4} pt", indent, rect.width, rect.height);
            println!("{}        * 圆角半径: TL={:.2} | TR={:.2} | BR={:.2} | BL={:.2} pt",
                     indent, rect.radius_top_left, rect.radius_top_right, rect.radius_bottom_right, rect.radius_bottom_left);
        }
        GeometryType::Circle => {
            let circle = &geom.data.circle;
            println!("{}      - 几何类型: [Circle (标准圆形)]", indent);
            println!("{}        * 圆心坐标: ({:.4}, {:.4}) pt", indent, circle.center.x, circle.center.y);
            println!("{}        * 物理半径: {:.4} pt", indent, circle.radius);
        }
        GeometryType::Ellipse => {
            let ellipse = &geom.data.ellipse;
            println!("{}      - 几何类型: [Ellipse (标准椭圆)]", indent);
            println!("{}        * 中心坐标: ({:.4}, {:.4}) pt", indent, ellipse.center.x, ellipse.center.y);
            println!("{}        * 物理半径: X半轴={:.4}, Y半轴={:.4} pt", indent, ellipse.rx, ellipse.ry);
        }
        GeometryType::Polygon => {
            let poly = &*geom.data.polygon;
            let vertices = poly.vertices.as_slice();
            println!("{}      - 几何类型: [Polygon (多边形)]", indent);
            println!("{}        * 顶点总数: {}", indent, vertices.len());
            for (v_idx, v) in vertices.iter().enumerate() {
                println!("{}          * Vertex [{}]: ({:.4}, {:.4}) pt", indent, v_idx, v.x, v.y);
            }
        }
        GeometryType::Path => {
            let path = &*geom.data.path;
            let pts = path.points.as_slice();
            let vbs = path.verbs.as_slice();
            println!("{}      - 几何类型: [Path (自由复合贝塞尔曲线段)] (物理点数: {}, 指令数: {})", indent, pts.len(), vbs.len());

            let mut pt_idx = 0;
            for (v_idx, verb) in vbs.iter().enumerate() {
                match verb {
                    PathVerb::MoveTo => {
                        if pt_idx < pts.len() {
                            println!("{}          * [{}] MoveTo : ({:.4}, {:.4}) pt", indent, v_idx, pts[pt_idx].x, pts[pt_idx].y);
                            pt_idx += 1;
                        }
                    }
                    PathVerb::LineTo => {
                        if pt_idx < pts.len() {
                            println!("{}          * [{}] LineTo : ({:.4}, {:.4}) pt", indent, v_idx, pts[pt_idx].x, pts[pt_idx].y);
                            pt_idx += 1;
                        }
                    }
                    PathVerb::QuadTo => {
                        if pt_idx + 1 < pts.len() {
                            println!("{}          * [{}] QuadTo : 控制点({:.4}, {:.4}) -> 终点({:.4}, {:.4}) pt",
                                     indent, v_idx, pts[pt_idx].x, pts[pt_idx].y, pts[pt_idx+1].x, pts[pt_idx+1].y);
                            pt_idx += 2;
                        }
                    }
                    PathVerb::CubicTo => {
                        if pt_idx + 2 < pts.len() {
                            println!("{}          * [{}] CubicTo: 控制点1({:.4}, {:.4}), 控制点2({:.4}, {:.4}) -> 终点({:.4}, {:.4}) pt",
                                     indent, v_idx, pts[pt_idx].x, pts[pt_idx].y, pts[pt_idx+1].x, pts[pt_idx+1].y, pts[pt_idx+2].x, pts[pt_idx+2].y);
                            pt_idx += 3;
                        }
                    }
                }
            }
            println!("{}        * 闭合标志 (closed): {}", indent, path.closed);
        }
    }
}

unsafe fn print_paint_details(paint: &Paint, label: &str, indent: &str) {
    match paint.ty {
        PaintType::None => {
            println!("{}      - {} 画笔: 无 (None / transparent)", indent, label);
        }
        PaintType::Solid => {
            let c = &paint.solid_color;
            println!("{}      - {} 画笔: [Solid 纯色填充]", indent, label);
            println!("{}        * RGBA 分量: (R:{:.4}, G:{:.4}, B:{:.4}, A:{:.4})", indent, c.r, c.g, c.b, c.a);
        }
        PaintType::Gradient => {
            let grad = &paint.gradient;
            println!("{}      - {} 画笔: [Gradient 渐变填充]", indent, label);
            println!("{}        * 渐变类别: {:?}", indent, grad.ty);
            println!("{}        * 空间坐标参照系 (Relative): {:?}", indent, grad.relative);
            println!("{}        * 几何映射参数: ", indent);
            println!("{}          * 起点/中心 (Start/Center): ({:.4}, {:.4}) pt", indent, grad.start.x, grad.start.y);
            println!("{}          * 终点/焦点 (End/Focal)   : ({:.4}, {:.4}) pt", indent, grad.end.x, grad.end.y);
            println!("{}          * 径向半径  (Radius)      : {:.4} pt", indent, grad.radius);
            println!("{}          * 锥形/旋转角(Angle)      : {:.4} rad ({:.2}°)", indent, grad.angle, grad.angle.to_degrees());
            println!("{}        * 延伸模式 (Spread Method): {:?}", indent, grad.spread);

            let stops = grad.stops.as_slice();
            println!("{}        * 渐变色标控制点 (Stops) 数量: {}", indent, stops.len());
            for (s_idx, stop) in stops.iter().enumerate() {
                let c = &stop.color;
                println!("{}          * Stops [{}]: 相对位置={:<5.2}%, 颜色=(R:{:.3}, G:{:.3}, B:{:.3}, A:{:.3})",
                         indent, s_idx, stop.offset * 100.0, c.r, c.g, c.b, c.a);
            }
        }
        PaintType::Tiling => {
            let tiling = &paint.tiling;
            println!("{}      - {} 画笔: [Tiling 矢量平铺图案]", indent, label);
            println!("{}        * 元胞尺寸: {:.4} x {:.4} pt", indent, tiling.width, tiling.height);
            println!("{}        * 平铺间距: dx={:.4}, dy={:.4} pt", indent, tiling.spacing_x, tiling.spacing_y);

            if !tiling.pattern.is_null() {
                println!("{}        ==================== 递归解析 Tiling 图案内部开始 ====================", indent);
                let sub_indent = format!("{}          ", indent);
                unsafe {
                    print_detailed_outline_impl(tiling.pattern, &sub_indent);
                }
                println!("{}        ==================== 递归解析 Tiling 图案内部结束 ====================", indent);
            }
        }
    }
}