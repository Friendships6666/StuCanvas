// src/types.rs
use std::mem::ManuallyDrop;

#[repr(C)]
pub struct CVec<T> {
    pub ptr: *mut T,
    pub len: usize,
    pub cap: usize,
}

impl<T> CVec<T> {
    pub fn from_vec(mut v: Vec<T>) -> Self {
        let ptr = v.as_mut_ptr();
        let len = v.len();
        let cap = v.capacity();
        std::mem::forget(v);
        Self { ptr, len, cap }
    }
    pub unsafe fn as_slice(&self) -> &[T] {
        if self.ptr.is_null() || self.len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(self.ptr, self.len)
        }
    }

    pub unsafe fn to_vec(self) -> Vec<T> {
        if self.ptr.is_null() {
            Vec::new()
        } else {
            Vec::from_raw_parts(self.ptr, self.len, self.cap)
        }
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct Point2D {
    pub x: f64,
    pub y: f64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct RGBA {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

// 【重构】：改为纯粹的列主序布局（Column-Major）
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct Transform2D {
    pub sx: f64, pub ky: f64, // 第一列 (Column 0)
    pub kx: f64, pub sy: f64, // 第二列 (Column 1)
    pub tx: f64, pub ty: f64, // 第三列 (Column 2)
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum PathVerb {
    MoveTo = 0,
    LineTo = 1,
    QuadTo = 2,
    CubicTo = 3,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum FillRule {
    None = 0,
    NonZero = 1,
    EvenOdd = 2,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum LineCap {
    Butt = 0,
    Round = 1,
    Square = 2,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum LineJoin {
    Miter = 0,
    Round = 1,
    Bevel = 2,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum GradientType {
    Linear = 0,
    Radial = 1,
    Conic = 2,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum SpreadMethod {
    Pad = 0,
    Repeat = 1,
    Reflect = 2,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum RelativeTo {
    Self_ = 0,
    Parent = 1,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum PaintType {
    None = 0,
    Solid = 1,
    Gradient = 2,
    Tiling = 3,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct GradientStop {
    pub offset: f64,
    pub color: RGBA,
}

#[repr(C)]
pub struct GradientPaint {
    pub ty: GradientType,
    pub start: Point2D,
    pub end: Point2D,
    pub radius: f64,
    pub angle: f64,
    pub spread: SpreadMethod,
    pub relative: RelativeTo,
    pub stops: CVec<GradientStop>,
}

#[repr(C)]
pub struct TilingPaint {
    pub pattern: *mut Outline,
    pub width: f64,
    pub height: f64,
    pub spacing_x: f64,
    pub spacing_y: f64,
}

#[repr(C)]
pub struct Paint {
    pub ty: PaintType,
    pub solid_color: RGBA,
    pub gradient: GradientPaint,
    pub tiling: TilingPaint,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum GeometryType {
    Path = 0,
    Line = 1,
    Rect = 2,
    Circle = 3,
    Ellipse = 4,
    Polygon = 5,
}

#[repr(C)]
pub struct PathGeometry {
    pub points: CVec<Point2D>,
    pub verbs: CVec<PathVerb>,
    pub closed: bool,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct LineGeometry {
    pub start: Point2D,
    pub end: Point2D,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct RectGeometry {
    pub origin: Point2D,
    pub width: f64,
    pub height: f64,
    pub radius_top_left: f64,
    pub radius_top_right: f64,
    pub radius_bottom_right: f64,
    pub radius_bottom_left: f64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct CircleGeometry {
    pub center: Point2D,
    pub radius: f64,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct EllipseGeometry {
    pub center: Point2D,
    pub rx: f64,
    pub ry: f64,
}

#[repr(C)]
pub struct PolygonGeometry {
    pub vertices: CVec<Point2D>,
}

#[repr(C)]
pub union GeometryUnion {
    pub path: ManuallyDrop<PathGeometry>,
    pub line: LineGeometry,
    pub rect: RectGeometry,
    pub circle: CircleGeometry,
    pub ellipse: EllipseGeometry,
    pub polygon: ManuallyDrop<PolygonGeometry>,
}

#[repr(C)]
pub struct Geometry {
    pub ty: GeometryType,
    pub data: GeometryUnion,
}

// 【新增】：共享几何体（极其适合直接绑定为 Vulkan 的 Vertex Buffer 单元）
#[repr(C)]
pub struct SharedGeometry {
    pub geometry_id: u32,
    pub geometry: Geometry,
}

// 【新增】：实例化渲染指令（极其适合直接绑定为 Vulkan 的 Instance Buffer / SSBO 元素）
#[repr(C)]
pub struct DrawInstance {
    pub geometry_id: u32,       // 映射到上面的 SharedGeometry ID
    pub transform: Transform2D,  // 独立的空间仿射变换矩阵
    pub opacity: f64,
    pub clip: bool,

    pub fill_paint: Paint,
    pub fill_rule: FillRule,

    pub stroke_paint: Paint,
    pub stroke_width: f64,
    pub stroke_cap: LineCap,
    pub stroke_join: LineJoin,
    pub miter_limit: f64,
    pub dash_array: CVec<f64>,
    pub dash_offset: f64,
}

// 【重构】：Outline 不再包含膨胀的线圈，而是包含“去重几何体池”和“实例化指令链”
#[repr(C)]
pub struct Outline {
    pub geometries: CVec<SharedGeometry>,
    pub instances: CVec<DrawInstance>,
}