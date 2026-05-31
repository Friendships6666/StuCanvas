/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

use std::collections::HashMap;
use std::mem::ManuallyDrop;
use typst::layout::{Frame, FrameItem, Transform, Point, Abs, Ratio};
use typst::visualize::{Geometry as TGeom, CurveItem, Paint as TPaint, Color, Gradient as TGrad};

use crate::types::*;
use crate::outline_builder::{RawOutlineBuilder};

// =====================================================================
// 1. 编译期去重几何池
// =====================================================================
pub struct GeometryPool {
    pub lookup: HashMap<(String, u16), u32>,
}

// =====================================================================
// 2. 基础数据类型转换函数
// =====================================================================

#[inline]
pub fn convert_transform(ts: &Transform) -> Transform2D {
    Transform2D {
        sx: ts.sx.get(),
        ky: ts.ky.get(),
        kx: ts.kx.get(),
        sy: ts.sy.get(),
        tx: ts.tx.to_pt(),
        ty: ts.ty.to_pt(),
    }
}

#[inline]
pub fn convert_color(color: &Color) -> RGBA {
    let rgb = color.to_rgb();
    RGBA { r: rgb.red, g: rgb.green, b: rgb.blue, a: rgb.alpha }
}

// =====================================================================
// 3. 材质（Paint）转换与多态解析
// =====================================================================
pub fn convert_paint(paint: &TPaint, origin: Point, transform: Transform) -> Paint {
    match paint {
        TPaint::Solid(color) => Paint {
            ty: PaintType::Solid,
            solid_color: convert_color(color),
            gradient: unsafe { std::mem::zeroed() },
            tiling: unsafe { std::mem::zeroed() },
        },
        TPaint::Gradient(grad) => {
            let mut stops = Vec::new();
            for (color, ratio) in grad.stops_ref() {
                stops.push(GradientStop {
                    offset: ratio.get(),
                    color: convert_color(color),
                });
            }

            let relative_val = match grad.relative() {
                typst::foundations::Smart::Auto => RelativeTo::Self_,
                typst::foundations::Smart::Custom(rel) => match rel {
                    typst::visualize::RelativeTo::Self_ => RelativeTo::Self_,
                    typst::visualize::RelativeTo::Parent => RelativeTo::Parent,
                }
            };

            let center = grad.center().unwrap_or_default();
            let focal_center = grad.focal_center().unwrap_or_default();
            let radius = grad.radius().map(|r| r.get()).unwrap_or(1.0);
            let angle = grad.angle().map(|a| a.to_rad()).unwrap_or(0.0);

            // 💡 【渐变核心解算说明】：
            // 在这一版的 Typst 中，线性渐变由 Angle 驱动，不存储物理的 start/end 坐标。
            // 因此，我们在此处将 start/end 初始化为默认的 (0,0)（因为 center 返回 None）。
            // 后端 Vulkan 渲染器（或 GPU Shader）应当采用 2D 渲染 standard：
            // 利用传入的 angle 结合当前几何体的实际包围盒，在渲染时动态计算出精确的渐变起点与终点。
            let gp = GradientPaint {
                ty: match grad {
                    TGrad::Linear(_) => GradientType::Linear,
                    TGrad::Radial(_) => GradientType::Radial,
                    TGrad::Conic(_) => GradientType::Conic,
                },
                start: Point2D { x: center.x.get(), y: center.y.get() },
                end: Point2D { x: focal_center.x.get(), y: focal_center.y.get() },
                radius,
                angle,
                spread: SpreadMethod::Pad, // 默认边缘平铺
                relative: relative_val,
                stops: CVec::from_vec(stops),
            };

            Paint {
                ty: PaintType::Gradient,
                solid_color: RGBA { r: 0., g: 0., b: 0., a: 0. },
                gradient: gp,
                tiling: unsafe { std::mem::zeroed() },
            }
        }
        TPaint::Tiling(tiling) => {
            // 递归编译平铺子帧并打包为 Box [3.2.1]
            let sub_outline = Box::new(compile_sub_frame(tiling.frame(), origin, transform));
            Paint {
                ty: PaintType::Tiling,
                solid_color: RGBA { r: 0., g: 0., b: 0., a: 0. },
                gradient: unsafe { std::mem::zeroed() },
                tiling: TilingPaint {
                    pattern: Box::into_raw(sub_outline), // 转换为裸指针供 C++ 承接
                    width: tiling.size().x.to_pt(),
                    height: tiling.size().y.to_pt(),
                    spacing_x: tiling.spacing().x.to_pt(),
                    spacing_y: tiling.spacing().y.to_pt(),
                },
            }
        }
    }
}

fn compile_sub_frame(frame: &Frame, origin: Point, transform: Transform) -> Outline {
    let mut pool = GeometryPool { lookup: HashMap::new() };
    let mut geometries = Vec::new();
    let mut instances = Vec::new();
    extract_frame_to_ffi(frame, origin, transform, &mut pool, &mut geometries, &mut instances);
    Outline {
        geometries: CVec::from_vec(geometries),
        instances: CVec::from_vec(instances),
    }
}

// =====================================================================
// 4. 核心物理帧转换为 FFI 格式
// =====================================================================
pub fn extract_frame_to_ffi(
    frame: &Frame,
    origin: Point,
    transform: Transform,
    pool: &mut GeometryPool,
    geometries: &mut Vec<SharedGeometry>,
    instances: &mut Vec<DrawInstance>,
) {
    for (pos, item) in frame.items() {
        match item {
            FrameItem::Shape(shape, _) => {
                let abs_pos = origin + *pos;
                let shape_transform = transform.pre_concat(Transform {
                    sx: Ratio::new(1.0), ky: Ratio::new(0.0), kx: Ratio::new(0.0), sy: Ratio::new(1.0),
                    tx: Abs::pt(abs_pos.x.to_pt()),
                    ty: Abs::pt(abs_pos.y.to_pt()),
                });

                // 创建几何体
                let mut geom = Geometry {
                    ty: GeometryType::Path,
                    data: unsafe { std::mem::zeroed() },
                };

                // 💡 【物理匹配】：Line、Rect、Curve 已经 100% 穷尽并覆盖了此版 Typst 的 Geometry 变体
                match &shape.geometry {
                    TGeom::Line(target) => {
                        geom.ty = GeometryType::Line;
                        geom.data.line = LineGeometry {
                            start: Point2D { x: 0., y: 0. },
                            end: Point2D { x: target.x.to_pt(), y: target.y.to_pt() },
                        };
                    }
                    TGeom::Rect(size) => {
                        geom.ty = GeometryType::Rect;
                        // 💡 带有圆角的矩形在排版生成 FrameItem 时已被 Typst 直接转换为 Curve(Path)，
                        // 这里的 Rect 只需极简、高性能地表达非圆角标准矩形。
                        geom.data.rect = RectGeometry {
                            origin: Point2D { x: 0., y: 0. },
                            width: size.x.to_pt(),
                            height: size.y.to_pt(),
                        };
                    }
                    TGeom::Curve(curve) => {
                        geom.ty = GeometryType::Path;
                        let mut pts = Vec::new();
                        let mut vbs = Vec::new();
                        for item in curve.0.iter() {
                            match item {
                                CurveItem::Move(p) => { pts.push(Point2D { x: p.x.to_pt(), y: p.y.to_pt() }); vbs.push(PathVerb::MoveTo); }
                                CurveItem::Line(p) => { pts.push(Point2D { x: p.x.to_pt(), y: p.y.to_pt() }); vbs.push(PathVerb::LineTo); }
                                CurveItem::Cubic(p1, p2, p3) => {
                                    pts.push(Point2D { x: p1.x.to_pt(), y: p1.y.to_pt() });
                                    pts.push(Point2D { x: p2.x.to_pt(), y: p2.y.to_pt() });
                                    pts.push(Point2D { x: p3.x.to_pt(), y: p3.y.to_pt() });
                                    vbs.push(PathVerb::CubicTo);
                                }
                                CurveItem::Close => {
                                    // 💡 【闭合动作化控制】：
                                    // 将“闭合”设计为一种特殊的 PathVerb 指令，处于动作流中，零点消耗 [1]
                                    vbs.push(PathVerb::Close);
                                }
                            }
                        }
                        geom.data.path = ManuallyDrop::new(PathGeometry {
                            points: CVec::from_vec(pts),
                            verbs: CVec::from_vec(vbs),
                        });
                    }
                }

                // 注册共享几何
                let geom_id = geometries.len() as u32;
                geometries.push(SharedGeometry {
                    geometry_id: geom_id,
                    geometry: geom,
                });

                // 分发实例化绘制指令
                let instance = DrawInstance {
                    geometry_id: geom_id,
                    transform: convert_transform(&shape_transform),
                    opacity: 1.0,
                    clip: false,
                    fill_paint: if let Some(fill) = &shape.fill {
                        convert_paint(fill, origin, transform)
                    } else {
                        Paint { ty: PaintType::None, solid_color: unsafe { std::mem::zeroed() }, gradient: unsafe { std::mem::zeroed() }, tiling: unsafe { std::mem::zeroed() } }
                    },
                    fill_rule: match shape.fill_rule {
                        typst::visualize::FillRule::NonZero => FillRule::NonZero,
                        typst::visualize::FillRule::EvenOdd => FillRule::EvenOdd,
                    },
                    stroke_paint: if let Some(stroke) = &shape.stroke {
                        convert_paint(&stroke.paint, origin, transform)
                    } else {
                        Paint { ty: PaintType::None, solid_color: unsafe { std::mem::zeroed() }, gradient: unsafe { std::mem::zeroed() }, tiling: unsafe { std::mem::zeroed() } }
                    },
                    stroke_width: shape.stroke.as_ref().map(|s| s.thickness.to_pt()).unwrap_or(0.0),
                    stroke_cap: shape.stroke.as_ref().map(|s| match s.cap {
                        typst::visualize::LineCap::Butt => LineCap::Butt,
                        typst::visualize::LineCap::Round => LineCap::Round,
                        typst::visualize::LineCap::Square => LineCap::Square,
                    }).unwrap_or(LineCap::Butt),
                    stroke_join: shape.stroke.as_ref().map(|s| match s.join {
                        typst::visualize::LineJoin::Miter => LineJoin::Miter,
                        typst::visualize::LineJoin::Round => LineJoin::Round,
                        typst::visualize::LineJoin::Bevel => LineJoin::Bevel,
                    }).unwrap_or(LineJoin::Miter),
                    miter_limit: shape.stroke.as_ref().map(|s| s.miter_limit.get()).unwrap_or(4.0),
                    dash_array: CVec::from_vec(shape.stroke.as_ref().and_then(|s| s.dash.as_ref().map(|d| d.array.iter().map(|l| l.to_pt()).collect())).unwrap_or_else(Vec::new)),
                    dash_offset: shape.stroke.as_ref().and_then(|s| s.dash.as_ref().map(|d| d.phase.to_pt())).unwrap_or(0.0),
                };

                instances.push(instance);
            }

            FrameItem::Text(text) => {
                let size_in_pt = text.size.to_pt() as f32;
                let face = text.font.ttf();
                let scale = size_in_pt / (face.units_per_em() as f32);
                let font_family = text.font.info().family.clone();

                let mut cursor_x = 0.0;
                for glyph in &text.glyphs {
                    let offset_x = glyph.x_offset.at(text.size).to_pt() as f32;
                    let offset_y = glyph.y_offset.at(text.size).to_pt() as f32;

                    // 1. 获取共享几何 ID（如果已存在则直接复用）
                    let key = (font_family.clone(), glyph.id);
                    let geom_id = if let Some(&id) = pool.lookup.get(&key) {
                        id
                    } else {
                        let mut builder = RawOutlineBuilder { contours: Vec::new(), current_contour: None };
                        face.outline_glyph(ttf_parser::GlyphId(glyph.id), &mut builder);

                        // 封存最后一个线圈
                        if let Some(c) = builder.current_contour.take() {
                            builder.contours.push(c);
                        }

                        let mut merged_points = Vec::new();
                        let mut merged_verbs = Vec::new();

                        // 已清理：彻底删除了未使用的 `let mut closed` 局部变量，消除了静态警告 [1]
                        for raw_contour in builder.contours {
                            merged_points.extend(raw_contour.points);
                            merged_verbs.extend(raw_contour.verbs);
                        }

                        let geom = Geometry {
                            ty: GeometryType::Path,
                            data: GeometryUnion {
                                path: ManuallyDrop::new(PathGeometry {
                                    points: CVec::from_vec(merged_points),
                                    verbs: CVec::from_vec(merged_verbs),
                                })
                            }
                        };

                        let id = geometries.len() as u32;
                        geometries.push(SharedGeometry {
                            geometry_id: id,
                            geometry: geom,
                        });
                        pool.lookup.insert(key, id);
                        id
                    };

                    // 2. 级联矩阵计算
                    let glyph_local = Transform {
                        sx: Ratio::new(scale as f64), ky: Ratio::new(0.0), kx: Ratio::new(0.0), sy: Ratio::new(-(scale as f64)),
                        tx: Abs::pt((cursor_x + offset_x) as f64),
                        ty: Abs::pt(offset_y as f64),
                    };

                    let combined = transform.pre_concat(glyph_local);
                    let final_transform = Transform {
                        sx: combined.sx, ky: combined.ky, kx: combined.kx, sy: combined.sy,
                        tx: Abs::pt(combined.tx.to_pt() + origin.x.to_pt() + pos.x.to_pt()),
                        ty: Abs::pt(combined.ty.to_pt() + origin.y.to_pt() + pos.y.to_pt()),
                    };

                    // 3. 仅分发轻量级绘制指令（只含 ID 和 2D 仿射变换矩阵）
                    let instance = DrawInstance {
                        geometry_id: geom_id,
                        transform: convert_transform(&final_transform),
                        opacity: 1.0,
                        clip: false,
                        fill_paint: convert_paint(&text.fill, origin, transform),
                        fill_rule: FillRule::NonZero,
                        stroke_paint: if let Some(stroke) = &text.stroke {
                            convert_paint(&stroke.paint, origin, transform)
                        } else {
                            Paint { ty: PaintType::None, solid_color: unsafe { std::mem::zeroed() }, gradient: unsafe { std::mem::zeroed() }, tiling: unsafe { std::mem::zeroed() } }
                        },
                        stroke_width: text.stroke.as_ref().map(|s| s.thickness.to_pt()).unwrap_or(0.0),
                        stroke_cap: text.stroke.as_ref().map(|s| match s.cap {
                            typst::visualize::LineCap::Butt => LineCap::Butt,
                            typst::visualize::LineCap::Round => LineCap::Round,
                            typst::visualize::LineCap::Square => LineCap::Square,
                        }).unwrap_or(LineCap::Butt),
                        stroke_join: text.stroke.as_ref().map(|s| match s.join {
                            typst::visualize::LineJoin::Miter => LineJoin::Miter,
                            typst::visualize::LineJoin::Round => LineJoin::Round,
                            typst::visualize::LineJoin::Bevel => LineJoin::Bevel,
                        }).unwrap_or(LineJoin::Miter),
                        miter_limit: text.stroke.as_ref().map(|s| s.miter_limit.get()).unwrap_or(4.0),
                        dash_array: CVec::from_vec(text.stroke.as_ref().and_then(|s| s.dash.as_ref().map(|d| d.array.iter().map(|l| l.to_pt()).collect())).unwrap_or_else(Vec::new)),
                        dash_offset: text.stroke.as_ref().and_then(|s| s.dash.as_ref().map(|d| d.phase.to_pt())).unwrap_or(0.0),
                    };
                    instances.push(instance);

                    cursor_x += glyph.x_advance.at(text.size).to_pt() as f32;
                }
            }

            FrameItem::Group(group) => {
                let new_origin = origin + *pos;
                let new_transform = transform.pre_concat(group.transform);

                if let Some(clip_curve) = &group.clip {
                    let mut pts = Vec::new();
                    let mut vbs = Vec::new();
                    for item in clip_curve.0.iter() {
                        match item {
                            CurveItem::Move(p) => { pts.push(Point2D { x: p.x.to_pt(), y: p.y.to_pt() }); vbs.push(PathVerb::MoveTo); }
                            CurveItem::Line(p) => { pts.push(Point2D { x: p.x.to_pt(), y: p.y.to_pt() }); vbs.push(PathVerb::LineTo); }
                            CurveItem::Cubic(p1, p2, p3) => {
                                pts.push(Point2D { x: p1.x.to_pt(), y: p1.y.to_pt() });
                                pts.push(Point2D { x: p2.x.to_pt(), y: p2.y.to_pt() });
                                pts.push(Point2D { x: p3.x.to_pt(), y: p3.y.to_pt() });
                                vbs.push(PathVerb::CubicTo);
                            }
                            CurveItem::Close => {
                                // 💡 【关键修复】：在此处的 Group Clip 路径解析中，也同样将 Close 动作正确填入！
                                // 避免含有剪裁蒙版的图容器（如圆角容器）因剪裁路径未闭合引起 Vulkan GPU 端 Stencil-Mask 渲染异常。
                                vbs.push(PathVerb::Close);
                            }
                        }
                    }
                    let clip_geom = Geometry {
                        ty: GeometryType::Path,
                        data: GeometryUnion {
                            path: ManuallyDrop::new(PathGeometry {
                                points: CVec::from_vec(pts),
                                verbs: CVec::from_vec(vbs),
                            })
                        }
                    };

                    let geom_id = geometries.len() as u32;
                    geometries.push(SharedGeometry {
                        geometry_id: geom_id,
                        geometry: clip_geom,
                    });

                    let clip_instance = DrawInstance {
                        geometry_id: geom_id,
                        transform: convert_transform(&new_transform),
                        opacity: 1.0,
                        clip: true,
                        fill_paint: Paint { ty: PaintType::None, solid_color: unsafe { std::mem::zeroed() }, gradient: unsafe { std::mem::zeroed() }, tiling: unsafe { std::mem::zeroed() } },
                        fill_rule: FillRule::NonZero,
                        stroke_paint: Paint { ty: PaintType::None, solid_color: unsafe { std::mem::zeroed() }, gradient: unsafe { std::mem::zeroed() }, tiling: unsafe { std::mem::zeroed() } },
                        stroke_width: 0.0,
                        stroke_cap: LineCap::Butt,
                        stroke_join: LineJoin::Miter,
                        miter_limit: 4.0,
                        dash_array: CVec::from_vec(Vec::new()),
                        dash_offset: 0.0,
                    };
                    instances.push(clip_instance);
                }

                extract_frame_to_ffi(&group.frame, new_origin, new_transform, pool, geometries, instances);
            }
            _ => {}
        }
    }
}

