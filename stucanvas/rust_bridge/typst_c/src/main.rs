// src/main.rs
use std::ffi::CString;

// 引入我们实现的 FFI 安全接口
use typst_c::{
    stucanvas_compile_typst,
    stucanvas_free_outline,
    stucanvas_free_string
};

// ------------------------------------------------------------
// 模板 1：正常的 Typst 模板 (用于测试成功路径)
// ------------------------------------------------------------
static GOOD_TEMPLATE: &str = r#"
#set text(font: ("Latin Modern Math", "Noto Sans CJK SC"))
#set page(width: 150pt, height: 100pt, fill: none, margin: 5pt)

#align(center + horizon)[
  #rect(
    width: 60pt,
    height: 20pt,
    fill: blue,
  )
]
"#;

// ------------------------------------------------------------
// 模板 2：含有语法错误的 Typst 模板 (用于测试失败路径)
// ------------------------------------------------------------
// 我们故意在 rect 属性中写了一个不存在的 "invalid_arg" 来触发编译报错
static BAD_TEMPLATE: &str = r#"
#set text(font: ("Latin Modern Math", "Noto Sans CJK SC"))
#set page(width: 150pt, height: 100pt, fill: none, margin: 5pt)

#align(center + horizon)[
  #rect(
    width: 60pt,
    invalid_arg: 123
  )
]
"#;

fn main() {
    let c_fonts_dir = CString::new("fonts").expect("Failed to create fonts dir string");

    println!("====================================================");
    println!("     Vulkan / C++ 兼容型 FFI 编译与异常拦截测试      ");
    println!("====================================================\n");

    // ----------------------------------------------------
    // [测试 1]：成功编译路径 (测试库的静默表现与物理内存回收)
    // ----------------------------------------------------
    println!("[测试 1] 开始进行【正常】Typst 代码编译测试...");
    let c_good_markup = CString::new(GOOD_TEMPLATE).expect("Failed to create CString");

    unsafe {
        // 调用编译。如果成功，stucanvas 库内部不会打印任何消息（完全静默）
        let result = stucanvas_compile_typst(c_good_markup.as_ptr(), c_fonts_dir.as_ptr());

        if !result.error_msg.is_null() {
            // 非预期情况：成功的代码不应该报错
            let err_cstr = std::ffi::CStr::from_ptr(result.error_msg);
            println!("❌ [测试 1] 意外失败！报错内容:\n{}", err_cstr.to_string_lossy());
            stucanvas_free_string(result.error_msg);
        } else {
            // 编译成功！这里由测试主控端（模拟的 C++ 侧）打印确认
            println!("▲ [测试 1] 编译成功！返回的几何体池与实例指针: {:?}", result.outline);

            // 物理内存释放，防止内存泄露
            stucanvas_free_outline(result.outline);
            println!("▲ [测试 1] 几何体与实例物理内存已安全回收。\n");
        }
    }

    // ----------------------------------------------------
    // [测试 2]：失败编译路径 (测试异常堆栈格式化抓取)
    // ----------------------------------------------------
    println!("[测试 2] 开始进行【错误】Typst 代码编译测试...");
    let c_bad_markup = CString::new(BAD_TEMPLATE).expect("Failed to create CString");

    unsafe {
        // 调用编译。如果失败，stucanvas 库会格式化具体报错，通过 error_msg 指针传出来
        let result = stucanvas_compile_typst(c_bad_markup.as_ptr(), c_fonts_dir.as_ptr());

        if !result.error_msg.is_null() {
            // 预期情况：成功捕获到错误
            println!("▲ [测试 2] 成功拦截到编译异常！");

            let err_cstr = std::ffi::CStr::from_ptr(result.error_msg);
            // 打印由 Rust 深度格式化并传出的 C 风格错误字符串
            println!("{}", err_cstr.to_string_lossy());

            // 必须调用该释放函数，回收 FFI 报错字符串内存，消灭泄露
            stucanvas_free_string(result.error_msg);
            println!("▲ [测试 2] 报错堆栈内存已安全释放。\n");
        } else {
            // 非预期情况：错误的代码不应该编译成功
            println!("❌ [测试 2] 意外成功，这不应该发生！指针: {:?}", result.outline);
            stucanvas_free_outline(result.outline);
        }
    }

    println!("====================================================");
    println!("               测试全部结束，系统处于健康状态          ");
    println!("====================================================");
}