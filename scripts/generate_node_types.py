#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# generate_node_types.py

import json
import os
import re

def load_config(json_path="node_types.json"):
    if not os.path.exists(json_path):
        raise FileNotFoundError(f"Configuration file not found: {json_path}")
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)

def to_camel_case(snake_str):
    """
    辅助函数：将大写的 SNAKE_CASE 转换为 CamelCase
    例如: POINT_2D_FREE -> Point2DFree
    """
    components = snake_str.lower().split('_')
    return "".join(x.title() for x in components)

def get_vtable_base_name(node):
    """
    统一的虚表基址命名析出器。
    """
    base = node.get("vtable")
    if not base:
        base = to_camel_case(node["name"])

    if base.endswith("_VTable"):
        base = base[:-7]
    elif base.endswith("VTable"):
        base = base[:-6]
    return base

def scan_cpp_source_code(root_dir="stucanvas"):
    """
    递归扫描所有 C++ 源码，自动忽略隐藏文件夹及构建目录。
    """
    all_content = []
    target_dir = root_dir if os.path.exists(root_dir) else "."

    for dirpath, dirnames, filenames in os.walk(target_dir):
        dirnames[:] = [d for d in dirnames if not d.startswith((".", "build", "target", "cmake-build"))]
        for filename in filenames:
            if filename.endswith((".cpp", ".hpp", ".h", ".cc", ".hh")):
                filepath = os.path.join(dirpath, filename)
                try:
                    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                        all_content.append(f.read())
                except Exception as e:
                    pass
    return "\n".join(all_content)

def check_symbol_exists(source_code, symbol_name):
    """
    严格的正则单词边界匹配
    """
    pattern = rf"\b{symbol_name}\b"
    return re.search(pattern, source_code) is not None

def generate_header(json_path="node_types.json", output_path="node_type.hpp"):
    config = load_config(json_path)

    worlds = config["WORLDS"]
    categories = config["CATEGORIES"]
    node_types = config["NODE_TYPES"]

    # 1. 扫描所有 C++ 文件
    cpp_source = scan_cpp_source_code("../stucanvas/sobject")

    content = []
    content.append("/***************************************************************************")
    content.append("* This file is AUTO-GENERATED from JSON. DO NOT MODIFY.                    *")
    content.append("***************************************************************************/")
    content.append("#pragma once")
    content.append("#include <cstdint>")
    content.append("#include <type_traits>")
    content.append("")
    content.append("namespace StuCanvas {")
    content.append("")
    content.append("    // ========================================== ")
    content.append("    // 64位高能位图类型标识 (支持多类别并存与编译期聚类检查)")
    content.append("    // ========================================== ")
    content.append("    enum class NodeType : uint64_t {")

    # 写入基础检索掩码
    content.append("        // ---- 核心检索掩码 ----")
    content.append(f"        MASK_WORLD    = 0xFF00'0000'0000'0000ULL,")
    content.append(f"        MASK_CATEGORY = 0x00FF'0000'0000'0000ULL,")
    content.append(f"        MASK_SPECIFIC = 0x0000'00FF'FFFF'FFFFULL,")
    content.append("")

    # 写入空间维度 (Worlds)
    content.append("        // ---- 空间维度区间 ----")
    for name, val in worlds.items():
        shifted_val = (val & 0xFF) << 56
        content.append(f"        {name:<21} = 0x{shifted_val:016X}ULL,")
    content.append("")

    # 写入聚类类别 (Categories)
    content.append("        // ---- 聚类大分类 (16位独立位标志) ----")
    for name, bit_index in categories.items():
        shifted_val = (1 << bit_index) << 40
        content.append(f"        {name:<21} = 0x{shifted_val:016X}ULL,")
    content.append("")

    # 写入具体节点类型 (Node Types)
    content.append("        // ---- 具体物理图元类型 (支持多类别并存) ----")
    for node in node_types:
        name = node["name"]
        world = node["world"]
        cats = node["categories"]
        spec_id = node["id"]
        w_val = worlds[world]

        merged_cat_val = 0
        for cat in cats:
            bit_idx = categories[cat]
            merged_cat_val |= (1 << bit_idx)

        s_val = spec_id & 0x0000_00FF_FFFF_FFFF
        combined_val = (w_val << 56) | (merged_cat_val << 40) | s_val
        content.append(f"        {name:<21} = 0x{combined_val:016X}ULL,")

    content.append("    };")
    content.append("")

    # 写入编译期检查断言
    content.append("    // ========================================== ")
    content.append("    // 零运行时开销的编译期聚类自检 (支持多类别交集检测)")
    content.append("    // ========================================== ")
    content.append("    [[nodiscard]] constexpr bool is_2d(NodeType t) noexcept {")
    content.append("        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::MASK_WORLD)) == static_cast<uint64_t>(NodeType::WORLD_2D);")
    content.append("    }")
    content.append("")
    content.append("    [[nodiscard]] constexpr bool is_3d(NodeType t) noexcept {")
    content.append("        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::MASK_WORLD)) == static_cast<uint64_t>(NodeType::WORLD_3D);")
    content.append("    }")
    content.append("")

    # 动态写入各分类的位检查判断函数
    for cat_name in categories.keys():
        pred_name = cat_name.lower().replace("cat_", "is_")
        content.append(f"    [[nodiscard]] constexpr bool {pred_name}(NodeType t) noexcept {{")
        content.append(f"        return (static_cast<uint64_t>(t) & static_cast<uint64_t>(NodeType::{cat_name})) != 0;")
        content.append("    }")
        content.append("")

    # ====================================================================
    # 3. 自动扫描、声明并定义 5 维虚函数表 (VTable)
    # ====================================================================
    content.append("    // ========================================================")
    content.append("    // 编译期静态虚函数表 (VTable) 内部模板定义 (支持按需延迟解算)")
    content.append("    // ========================================================")
    content.append("    template <typename T> struct SObjectGraph;")
    content.append("    template <typename T> struct SObject;")
    content.append("    template <typename T>")
    content.append("    struct SObjectVTable")
    content.append("    {")
    content.append("    void (*solver)(SObjectGraph<T>&, SObject<T>&) = nullptr;")
    content.append("    void (*discretize_to_points)(SObjectGraph<T>&, SObject<T>&, double) = nullptr;")
    content.append("    void (*discretize_to_strips)(SObjectGraph<T>&, SObject<T>&, double) = nullptr;")
    content.append("    void (*discretize_to_triangles)(SObjectGraph<T>&, SObject<T>&, double) = nullptr;")
    content.append("    void (*render)(SObjectGraph<T>&, SObject<T>&) = nullptr;")
    content.append("    };")
    content.append("")

    # 虚表定义与函数指针的映射规则
    vtable_fields = [
        ("solver", "Solve{base}"),
        ("discretize_to_points", "Discretize{base}_Points"),
        ("discretize_to_strips", "Discretize{base}_Strips"),
        ("discretize_to_triangles", "Discretize{base}_Triangles"),
        ("render", "Render{base}")
    ]

    processed_bases = set()
    found_declarations = []
    vtable_definitions = []

    # 扫描与生成定义
    for node in node_types:
        base = get_vtable_base_name(node)

        if base in processed_bases:
            continue
        processed_bases.add(base)

        assignments = []
        for field, pattern in vtable_fields:
            func_name = pattern.format(base=base)

            # 【物理检测】：检查该 C++ 符号是否存在
            if check_symbol_exists(cpp_source, func_name):
                if field.startswith("discretize_"):
                    found_declarations.append(f"    template <typename T> void {func_name}(SObjectGraph<T>&, SObject<T>&, double) noexcept;")
                else:
                    found_declarations.append(f"    template <typename T> void {func_name}(SObjectGraph<T>&, SObject<T>&) noexcept;")
                assignments.append(f"        .{field} = &{func_name}<T>")
            else:
                assignments.append(f"        .{field} = nullptr")

        vdef = []
        vdef.append(f"    template <typename T>")
        vdef.append(f"    inline const SObjectVTable<T> {base}_VTable = {{")
        vdef.append(",\n".join(assignments))
        vdef.append(f"    }};")
        vtable_definitions.append("\n".join(vdef))

    # 1. 写入找到的外部函数的前向声明 (解耦编译依赖)
    content.append("    // ---- 物理存在的解算与离散化模板函数前向声明 ----")
    content.extend(sorted(list(set(found_declarations))))
    content.append("")

    # 2. 直接写入自动拼装、零开销内联的 VTable 变量定义 (去除了之前的所有冗余 extern 声明) [1.1.2]
    content.append("    // ---- 自动拼装完毕的 C++17 内联全局虚表定义 (声明与定义合一) ----")
    content.append("\n\n".join(vtable_definitions))
    content.append("")

    content.append("} // namespace StuCanvas")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(content))
    print(f"[SUCCESS] JSON and source code parsed. Clean VTables exported to: {output_path}")

if __name__ == "__main__":
    generate_header()