import os

def create_combined_output(output_filename="project_summary.txt", items_to_scan=None, exclude_dirs=None):
    """
    读取指定的目录或单个文件，
    将它们的内容和路径汇总写入到单个文本文件中。
    支持配置排除目录。
    """
    current_directory = os.getcwd()

    # 如果未指定，则使用默认的扫描列表
    if items_to_scan is None:
        items_to_scan = ["../stucanvas/sobject"]

    # 💡 新增：需要排除的目录列表
    if exclude_dirs is None:
        exclude_dirs = ["../stucanvas/assets","../stucanvas/rust_bridge/typst_c/target","../stucanvas/rust_bridge/typst_c/Cargo.lock","../stucanvas/rust_bridge/typst_c/Cargo.toml"]

    all_file_paths = []

    # 将排除目录转换为规范化的绝对路径集合，便于后续高效比对
    exclude_abs_paths = {
        os.path.normpath(os.path.join(current_directory, d)) for d in exclude_dirs
    }

    try:
        with open(output_filename, "w", encoding="utf-8") as output_file:

            for item in items_to_scan:
                full_path = os.path.normpath(os.path.join(current_directory, item))

                if not os.path.exists(full_path):
                    print(f"警告：路径 '{item}' 不存在，已跳过。")
                    continue

                # 💡 情况 1：如果是具体的文件
                if os.path.isfile(full_path):
                    process_single_file(full_path, current_directory, output_file, all_file_paths)

                # 💡 情况 2：如果是目录
                elif os.path.isdir(full_path):
                    for root, dirs, files in os.walk(full_path):
                        # 💡 核心修改：通过原位修改 dirs 列表，阻止 os.walk 递归进入排除的目录
                        # os.path.normpath 会自动处理末尾的斜杠，确保对比一致
                        dirs[:] = [
                            d for d in dirs
                            if os.path.normpath(os.path.join(root, d)) not in exclude_abs_paths
                        ]

                        for filename in files:
                            file_path = os.path.join(root, filename)
                            process_single_file(file_path, current_directory, output_file, all_file_paths)

            # --- 第二部分：汇总列表 ---
            output_file.write("========================================\n")
            output_file.write("           文件路径汇总\n")
            output_file.write("========================================\n\n")

            if all_file_paths:
                for path in all_file_paths:
                    output_file.write(f"{path}\n")
            else:
                output_file.write("未找到任何有效文件。\n")

        print(f"成功！已汇总至 '{output_filename}'。")

    except Exception as e:
        print(f"发生错误: {e}")

def process_single_file(file_path, base_dir, output_file, path_list):
    """
    读取单个文件内容并写入输出文件的辅助函数
    """
    relative_path = os.path.relpath(file_path, base_dir)
    path_list.append(relative_path)

    try:
        output_file.write(f"--- 文件路径: {relative_path} ---\n\n")
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            output_file.write(f.read())
            output_file.write("\n\n")
    except Exception as e:
        output_file.write(f"*** 无法读取文件: {relative_path} | 错误: {e} ***\n\n")

if __name__ == "__main__":
    # 可以直接运行，也可以在此处传入自定义的扫描和排除列表
    create_combined_output()