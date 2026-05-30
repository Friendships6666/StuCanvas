// src/outline_builder.rs
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
        // 如果上一个线圈未闭合就遇到了新的起点，强制封存
        if let Some(c) = self.current_contour.take() {
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
            c.points.push(Point2D { x: x1 as f64, y: y1 as f64 });
            c.points.push(Point2D { x: x as f64, y: y as f64 });
            c.verbs.push(PathVerb::QuadTo);
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
        // 遇到闭合事件：将当前线圈设为闭合状态并封存
        if let Some(mut c) = self.current_contour.take() {
            c.closed = true;
            self.contours.push(c);
        }
    }
}