import os

def create_combined_output(output_filename="project_summary.txt"):
    """
    读取 'include' 和 'src' 目录下的所有文件，
    将它们的内容和路径汇总写入到单个文本文件中。
    """
    # 获取当前工作目录
    current_directory = os.getcwd()
    # 定义要扫描的目录
    directories_to_scan = ["include", "src"]
    # 用于存储所有文件路径的列表，以便最后汇总
    all_file_paths = []

    try:
        # 打开唯一的输出文件
        with open(output_filename, "w", encoding="utf-8") as output_file:
            
            # --- 第一部分：遍历并写入文件内容 ---
            
            # 遍历 'include' 和 'src' 目录
            for directory in directories_to_scan:
                dir_path = os.path.join(current_directory, directory)
                
                # 检查目录是否存在，如果不存在则跳过
                if not os.path.exists(dir_path):
                    print(f"警告：目录 '{dir_path}' 不存在，已跳过。")
                    continue

                # 遍历目录树
                for root, _, files in os.walk(dir_path):
                    for filename in files:
                        # 构建文件的完整路径
                        file_path = os.path.join(root, filename)
                        # 获取相对于当前目录的相对路径
                        relative_path = os.path.relpath(file_path, current_directory)
                        
                        # 将路径存入列表，用于最后的汇总
                        all_file_paths.append(relative_path)
                        
                        try:
                            # 在文件内容前写入路径作为标题
                            output_file.write(f"--- 文件路径: {relative_path} ---\n\n")
                            
                            # 读取并写入文件内容
                            with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                                content = f.read()
                                output_file.write(content)
                                # 在每个文件内容后添加换行符以作分隔
                                output_file.write("\n\n")
                        except Exception as e:
                            # 如果文件读取失败，则记录一条错误信息
                            output_file.write(f"*** 无法读取文件: {relative_path} | 错误: {e} ***\n\n")

            # --- 第二部分：在文件末尾写入路径汇总 ---
            
            # 添加一个清晰的分隔符
            output_file.write("========================================\n")
            output_file.write("           文件路径汇总\n")
            output_file.write("========================================\n\n")
            
            # 检查是否找到了文件
            if all_file_paths:
                # 写入所有收集到的文件路径
                for path in all_file_paths:
                    output_file.write(f"{path}\n")
            else:
                output_file.write("在 'include' 或 'src' 目录中未找到任何文件。\n")
        
        print(f"成功！所有内容和路径汇总已写入 '{output_filename}'。")

    except IOError as e:
        print(f"发生文件操作错误: {e}")
    except Exception as e:
        print(f"发生未知错误: {e}")

# 脚本执行入口
if __name__ == "__main__":
    create_combined_output()
