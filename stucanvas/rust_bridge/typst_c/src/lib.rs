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
    // 显式指定 null_mut 的泛型类型，消除 "Type annotations needed" 报错
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

    // 通过 .output 获取真正的 Result<PagedDocument, TypstAsLibError>
    match engine.compile::<PagedDocument>().output {
        Ok(compiled_doc) => {
            // 编译成功路径：保持绝对静默，禁止任何 println!
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
            // 编译失败路径：此时才格式化并输出错误信息
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

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_outline(outline: *mut Outline) {
    if outline.is_null() {
        return;
    }

    // 💡 针对 Rust 2024 规范：使用显式安全作用域包裹 FFI 裸指针和内存回收操作，零警告编译 [1]
    let out_box = unsafe { Box::from_raw(outline) };
    let geometries = unsafe { out_box.geometries.into_vec() };
    let instances = unsafe { out_box.instances.into_vec() };

    for shared_geom in geometries {
        unsafe { free_geometry_resources(shared_geom.geometry) };
    }

    for instance in instances {
        unsafe {
            free_paint_resources(instance.fill_paint);
            free_paint_resources(instance.stroke_paint);

            // 释放可能分配的虚线 Dash 堆内存 [1]
            let _ = instance.dash_array.into_vec();
        }
    }
}


unsafe fn free_geometry_resources(geometry: Geometry) {
    match geometry.ty {
        GeometryType::Path => {
            let path_geom = unsafe { std::mem::ManuallyDrop::into_inner(geometry.data.path) };
            // points 和 verbs 拥有外部堆物理指针，必须手动回收 [15.1]
            let _ = unsafe { path_geom.points.into_vec() };
            let _ = unsafe { path_geom.verbs.into_vec() };
        }
        // 💡 显式标明 Line 和 Rect 是平铺内存，无任何外部堆分配，自动伴随释放
        GeometryType::Line => {}
        GeometryType::Rect => {}
    }
}

unsafe fn free_paint_resources(paint: Paint) {
    match paint.ty {
        PaintType::Gradient => {
            // 💡 【物理泄露修复】：回收 Gradient 底层 stops 拥有的堆内存 [1]
            let _ = unsafe { paint.gradient.stops.into_vec() };
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