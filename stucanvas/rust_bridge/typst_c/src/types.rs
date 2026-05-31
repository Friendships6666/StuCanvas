/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

use std::mem::ManuallyDrop;

// =====================================================================
// 1. 跨语言 FFI 专用矢量容器 (CVec) 及其生命周期控制
// =====================================================================

#[repr(C)]
pub struct CVec<T> {
    pub ptr: *mut T,
    pub len: usize,
    pub cap: usize,
}

impl<T> CVec<T> {
    /// 构造一个物理内存安全的空 FFI 向量
    #[inline]
    pub fn empty() -> Self {
        Self {
            ptr: std::ptr::null_mut(),
            len: 0,
            cap: 0,
        }
    }

    /// 从 Rust 的原生 Vec 封存并转移所有权至 FFI 容器
    pub fn from_vec(mut v: Vec<T>) -> Self {
        let ptr = v.as_mut_ptr();
        let len = v.len();
        let cap = v.capacity();
        std::mem::forget(v); // 阻止 Rust 自动释放堆内存
        Self { ptr, len, cap }
    }

    /// 转换为只读切片视图
    pub unsafe fn as_slice(&self) -> &[T] {
        if self.ptr.is_null() || self.len == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(self.ptr, self.len)
        }
    }

    /// 💡 【新增】：将 CVec 物理内存无损恢复为 Rust 的 Vec 并收回堆控制权（用于安全释放）
    pub unsafe fn into_vec(self) -> Vec<T> {
        if self.ptr.is_null() {
            Vec::new()
        } else {
            Vec::from_raw_parts(self.ptr, self.len, self.cap)
        }
    }
}

// =====================================================================
// 2. 基础数学与颜色结构体 (FFI 对齐)
// =====================================================================

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

/// 列主序 2D 仿射变换矩阵（与 Eigen / GPU Shader 完美 1-to-1 物理对齐）
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct Transform2D {
    pub sx: f64, pub ky: f64, // 第一列 (Column 0)
    pub kx: f64, pub sy: f64, // 第二列 (Column 1)
    pub tx: f64, pub ty: f64, // 第三列 (Column 2)
}

// =====================================================================
// 3. FFI 强类型枚举 (强制 u8 占用 1 字节，完美对应 C++ 的 enum class : uint8_t)
// =====================================================================

#[repr(u8)]
#[derive(Debug, Copy, Clone)]
pub enum PathVerb {
    MoveTo = 0,
    LineTo = 1,
    CubicTo = 2, // 已剔除无用的 QuadTo 分支，紧凑对齐
    Close = 3,
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

// =====================================================================
// 4. 画笔与材质数据结构
// =====================================================================

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

// =====================================================================
// 5. 多态几何体数据结构（已彻底精简冗余几何体）
// =====================================================================

#[repr(u8)]
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum GeometryType {
    Path = 0,
    Line = 1,
    Rect = 2,
}

#[repr(C)]
pub struct PathGeometry {
    pub points: CVec<Point2D>,
    pub verbs: CVec<PathVerb>,
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
    // 💡 属性瘦身：移除了 radius_* 圆角参数（圆角矩形已被自动降级并表示为 Path 几何体）
}

#[repr(C)]
pub union GeometryUnion {
    pub path: ManuallyDrop<PathGeometry>,
    pub line: LineGeometry,
    pub rect: RectGeometry,
}

#[repr(C)]
pub struct Geometry {
    pub ty: GeometryType,
    pub data: GeometryUnion,
}

// =====================================================================
// 6. 共享缓冲与实例化绘制结构（Vulkan 极其友好型）
// =====================================================================

#[repr(C)]
pub struct SharedGeometry {
    pub geometry_id: u32,
    pub geometry: Geometry,
}

#[repr(C)]
pub struct DrawInstance {
    pub geometry_id: u32,
    pub transform: Transform2D,
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

#[repr(C)]
pub struct Outline {
    pub geometries: CVec<SharedGeometry>,
    pub instances: CVec<DrawInstance>,
}