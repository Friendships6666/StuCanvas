/***************************************************************************
* Copyright (c) 2026 Tian Yuxuan (Friendships666)                          *
*                                                                          *
* Distributed under the terms of the MIT License.                          *
*                                                                          *
* The full license is in the file LICENSE, distributed with this software. *
***************************************************************************/

pub mod types;
pub mod outline_builder;
pub mod parser;
pub mod printer;

pub use printer::print_detailed_outline;

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::fs;
use std::path::Path;
use std::collections::HashMap;
use std::mem::ManuallyDrop; // 💡 【关键修复 1】：显式导入 ManuallyDrop，消除 E0433 报错
use typst_as_lib::TypstEngine;
use typst::layout::{PagedDocument, Point, Transform};

use crate::types::*;
use crate::parser::{GeometryPool, extract_frame_to_ffi};

#[repr(C)]
pub struct CompileResult {
    pub outline: *mut Outline,
    pub error_msg: *mut c_char,
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_compile_typst(
    markup_str: *const c_char,
    fonts_dir: *const c_char,
) -> CompileResult {
    if markup_str.is_null() || fonts_dir.is_null() {
        return CompileResult {
            outline: std::ptr::null_mut::<Outline>(),
            error_msg: CString::new("❌ [FFI Error] 传入的 C 字符串指针为空！").unwrap().into_raw(),
        };
    }

    let c_markup = unsafe { CStr::from_ptr(markup_str) }.to_string_lossy().into_owned();
    let c_fonts_path = unsafe { CStr::from_ptr(fonts_dir) }.to_string_lossy().into_owned();

    let mut font_buffers: Vec<Vec<u8>> = Vec::new();
    if let Ok(entries) = fs::read_dir(Path::new(&c_fonts_path)) {
        for entry in entries.flatten() {
            let path = entry.path();
            if let Some(ext) = path.extension() {
                let ext_str = ext.to_string_lossy().to_lowercase();
                if ext_str == "ttf" || ext_str == "otf" || ext_str == "ttc" {
                    if let Ok(data) = fs::read(&path) {
                        font_buffers.push(data);
                    }
                }
            }
        }
    }
    let fonts_slice: Vec<&[u8]> = font_buffers.iter().map(|b| b.as_slice()).collect();

    let engine = TypstEngine::builder()
        .main_file(c_markup.as_str())
        .fonts(fonts_slice)
        .with_package_file_resolver()
        .build();

    match engine.compile::<PagedDocument>().output {
        Ok(compiled_doc) => {
            let mut pool = GeometryPool { lookup: HashMap::new() };
            let mut geometries = Vec::new();
            let mut instances = Vec::new();

            if let Some(page) = compiled_doc.pages.first() {
                extract_frame_to_ffi(&page.frame, Point::zero(), Transform::identity(), &mut pool, &mut geometries, &mut instances);
            }

            let final_outline = Box::new(Outline {
                geometries: CVec::from_vec(geometries),
                instances: CVec::from_vec(instances),
            });

            CompileResult {
                outline: Box::into_raw(final_outline),
                error_msg: std::ptr::null_mut::<c_char>(),
            }
        }
        Err(error) => {
            let err_str = format!("❌ [Typst 语法/编译错误]\n{:#?}", error);

            let c_err = CString::new(err_str).unwrap_or_else(|_| CString::new("Unknown compile error").unwrap());
            CompileResult {
                outline: std::ptr::null_mut::<Outline>(),
                error_msg: c_err.into_raw(),
            }
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_print_detailed_outline(outline: *const Outline) {
    if outline.is_null() {
        return;
    }
    unsafe { print_detailed_outline(outline) };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_string(err_str: *mut c_char) {
    if err_str.is_null() {
        return;
    }
    let _ = unsafe { CString::from_raw(err_str) };
}

// =====================================================================
// 5. FFI 内存自动析构回收模块（高度兼容 C++ 五法则值类型）
// =====================================================================

// 💡 【关键修复 2】：在 lib.rs 内部显式实现 FFI 容器转换助手，消除 E0425 报错
unsafe fn cvec_to_vec<T>(cvec: CVec<T>) -> Vec<T> {
    if cvec.ptr.is_null() {
        Vec::new()
    } else {
        Vec::from_raw_parts(cvec.ptr, cvec.len as usize, cvec.cap as usize)
    }
}

unsafe fn free_geometry_resources(geometry: Geometry) {
    match geometry.ty {
        GeometryType::Path => {
            let path_geom = unsafe { ManuallyDrop::into_inner(geometry.data.path) };
            let _ = unsafe { cvec_to_vec(path_geom.points) };
            let _ = unsafe { cvec_to_vec(path_geom.verbs) };
        }
        // 💡 Line 和 Rect 仅包含平铺 POD 数据，属于连续内存，自动伴随释放
        GeometryType::Line => {}
        GeometryType::Rect => {}
    }
}

unsafe fn free_paint_resources(paint: Paint) {
    match paint.ty {
        PaintType::Gradient => {
            // 回收 Gradient 底层 stops 拥有的堆内存 [1]
            let _ = unsafe { cvec_to_vec(paint.gradient.stops) };
        }
        PaintType::Tiling => {
            let tiling = paint.tiling;
            if !tiling.pattern.is_null() {
                // 深度递归销毁 Tiling 嵌套，彻底根除物理内存溢出风险 [1]
                unsafe { stucanvas_free_outline(tiling.pattern) };
            }
        }
        _ => {}
    }
}

/// # Safety
/// 💡 【双析构支持 1】：专门用于 C++ 侧在栈上/容器内以【值类型】直接析构 Outline 时调用。
/// 仅清理内部持有的 geometries/instances 堆内存，绝对不释放传入的 outline 指针本身。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_outline_members(outline: *mut Outline) {
    if outline.is_null() {
        return;
    }
    // 获取可变引用，直接在当前内存块上重置内部 CVector 容器
    let out = unsafe { &mut *outline };

    let instances = unsafe {
        cvec_to_vec(std::mem::replace(
            &mut out.instances,
            CVec { ptr: std::ptr::null_mut(), len: 0, cap: 0 }
        ))
    };
    for instance in instances {
        unsafe {
            free_paint_resources(instance.fill_paint);
            free_paint_resources(instance.stroke_paint);
            let _ = cvec_to_vec(instance.dash_array);
        }
    }

    let geometries = unsafe {
        cvec_to_vec(std::mem::replace(
            &mut out.geometries,
            CVec { ptr: std::ptr::null_mut(), len: 0, cap: 0 }
        ))
    };
    for sg in geometries {
        unsafe { free_geometry_resources(sg.geometry) };
    }
}

/// # Safety
/// 💡 【双析构支持 2】：专门用于 C++ 侧以【智能指针】托管释放从 compile 接口返回的物理指针。
/// 深度清理 Outline 内部所有成员后，最后收回并销毁其自身在堆上的 Box 空间。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_outline(outline: *mut Outline) {
    if outline.is_null() {
        return;
    }
    // 1. 先安全清理 Outline 内部的所有子成员堆资源
    unsafe { stucanvas_free_outline_members(outline) };
    // 2. 重新打包为 Box，收回并彻底销毁 Outline 自身的 48 字节堆空间
    let _ = unsafe { Box::from_raw(outline) };
}