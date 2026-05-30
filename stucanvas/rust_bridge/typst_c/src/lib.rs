// src/lib.rs
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
    // 【修复】：显示指定 null_mut 的泛型类型，消除 "Type annotations needed" 报错
    if markup_str.is_null() || fonts_dir.is_null() {
        return CompileResult {
            outline: std::ptr::null_mut::<Outline>(),
            error_msg: CString::new("❌ [FFI Error] 传入的 C 字符串指针为空！").unwrap().into_raw(),
        };
    }

    let c_markup = CStr::from_ptr(markup_str).to_string_lossy().into_owned();
    let c_fonts_path = CStr::from_ptr(fonts_dir).to_string_lossy().into_owned();

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

    // 【修复】：通过 .output 获取真正的 Result<PagedDocument, TypstAsLibError>
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
    print_detailed_outline(outline);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_string(err_str: *mut c_char) {
    if err_str.is_null() {
        return;
    }
    let _ = CString::from_raw(err_str);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn stucanvas_free_outline(outline: *mut Outline) {
    if outline.is_null() {
        return;
    }

    let out_box = Box::from_raw(outline);
    let geometries = out_box.geometries.to_vec();
    let instances = out_box.instances.to_vec();

    for shared_geom in geometries {
        free_geometry_resources(shared_geom.geometry);
    }

    for instance in instances {
        free_paint_resources(instance.fill_paint);
        free_paint_resources(instance.stroke_paint);
        instance.dash_array.to_vec();
    }
}

unsafe fn free_geometry_resources(geometry: Geometry) {
    match geometry.ty {
        GeometryType::Path => {
            let path_geom = std::mem::ManuallyDrop::into_inner(geometry.data.path);
            path_geom.points.to_vec();
            path_geom.verbs.to_vec();
        }
        GeometryType::Polygon => {
            let poly_geom = std::mem::ManuallyDrop::into_inner(geometry.data.polygon);
            poly_geom.vertices.to_vec();
        }
        _ => {}
    }
}

unsafe fn free_paint_resources(paint: Paint) {
    match paint.ty {
        PaintType::Gradient => {
            paint.gradient.stops.to_vec();
        }
        PaintType::Tiling => {
            let tiling = paint.tiling;
            if !tiling.pattern.is_null() {
                stucanvas_free_outline(tiling.pattern);
            }
        }
        _ => {}
    }
}