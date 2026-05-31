/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

use crate::types::{Point2D, PathVerb};

#[derive(Clone)]
pub struct RawContour {
    pub points: Vec<Point2D>,
    pub verbs: Vec<PathVerb>,
    pub closed: bool,
}

pub struct RawOutlineBuilder {
    pub contours: Vec<RawContour>,
    pub current_contour: Option<RawContour>,
}

impl ttf_parser::OutlineBuilder for RawOutlineBuilder {
    fn move_to(&mut self, x: f32, y: f32) {
        // 如果上一个线圈未闭合就遇到了新的起点，强制补齐 Close 动词并归档
        if let Some(mut c) = self.current_contour.take() {
            // 💡 修正：使用 matches! 避免对未推导 PartialEq 类型的 PathVerb 使用 != 运算符
            if !matches!(c.verbs.last(), Some(&PathVerb::Close)) {
                c.verbs.push(PathVerb::Close);
            }
            self.contours.push(c);
        }
        self.current_contour = Some(RawContour {
            points: vec![Point2D { x: x as f64, y: y as f64 }],
            verbs: vec![PathVerb::MoveTo],
            closed: false,
        });
    }

    fn line_to(&mut self, x: f32, y: f32) {
        if let Some(c) = &mut self.current_contour {
            c.points.push(Point2D { x: x as f64, y: y as f64 });
            c.verbs.push(PathVerb::LineTo);
        }
    }

    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        if let Some(c) = &mut self.current_contour {
            // 💡 【2阶贝塞尔转3阶贝塞尔无损升阶】
            let p0 = *c.points.last().unwrap_or(&Point2D { x: 0.0, y: 0.0 });
            let p1 = Point2D { x: x1 as f64, y: y1 as f64 };
            let p2 = Point2D { x: x as f64, y: y as f64 };

            // 三阶控制点 1: C1 = p0 + 2/3 * (p1 - p0)
            let c1 = Point2D {
                x: p0.x + (2.0 / 3.0) * (p1.x - p0.x),
                y: p0.y + (2.0 / 3.0) * (p1.y - p0.y),
            };

            // 三阶控制点 2: C2 = p2 + 2/3 * (p1 - p2)
            let c2 = Point2D {
                x: p2.x + (2.0 / 3.0) * (p1.x - p2.x),
                y: p2.y + (2.0 / 3.0) * (p1.y - p2.y),
            };

            // 压入转换后的 3 个物理点与 CubicTo 动词
            c.points.push(c1);
            c.points.push(c2);
            c.points.push(p2);
            c.verbs.push(PathVerb::CubicTo);
        }
    }

    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        if let Some(c) = &mut self.current_contour {
            c.points.push(Point2D { x: x1 as f64, y: y1 as f64 });
            c.points.push(Point2D { x: x2 as f64, y: y2 as f64 });
            c.points.push(Point2D { x: x as f64, y: y as f64 });
            c.verbs.push(PathVerb::CubicTo);
        }
    }

    fn close(&mut self) {
        // 遇到闭合事件：将 Close 动词真实注入到 verbs 队列中并归档
        if let Some(mut c) = self.current_contour.take() {
            // 💡 修正：同上，使用 matches! 规避 PartialEq 缺失问题
            if !matches!(c.verbs.last(), Some(&PathVerb::Close)) {
                c.verbs.push(PathVerb::Close);
            }
            c.closed = true;
            self.contours.push(c);
        }
    }
}