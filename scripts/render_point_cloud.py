import sys
import os
import time
import numpy as np

# =========================================================================
# 💡 强行启用 Headless 无界面后端，100% 绕开 PySide6 导入和 X11 通道锁死！
# =========================================================================
import matplotlib
matplotlib.use('Agg')

import matplotlib.pyplot as plt

def main():
    # 数据路径
    filename = "/home/friendships666/Projects/StuCanvas/cmake-build-release/points.txt"
    if not os.path.exists(filename):
        filename = "/home/friendships666/Projects/StuCanvas/cmake-build-release/points.txt"

    if not os.path.exists(filename):
        print(f"[Error] 找不到任何数据文件。")
        return

    print(f"正在读取数据: {filename}")

    # 自动兼容处理有 7 行文件头的 .ply 标准点云格式与普通的 .txt 格式
    if filename.endswith(".ply"):
        data = np.loadtxt(filename, skiprows=7, dtype=np.float64)
    else:
        data = np.loadtxt(filename, dtype=np.float64)

    if data.ndim == 1:  # 只有一个点的情况
        data = data.reshape(1, -1)

    num_cols = data.shape[1]
    num_points = data.shape[0]
    print(f"检测到 {num_cols}D 数据，共 {num_points} 个点。")

    # 坐标中心化（防止大坐标下产生数值偏移）
    offsets = np.median(data, axis=0)
    data_rendered = data - offsets

    # 创建 Matplotlib 绘图画布
    fig = plt.figure(figsize=(10.0, 8.0))

    if num_cols == 2:
        # ==================== 2D 纯 CPU 离线渲染 ====================
        ax = fig.add_subplot(111)
        ax.scatter(data_rendered[:, 0], data_rendered[:, 1], color='#FF5050', s=4, alpha=0.8)

        ax.set_title(f"2D CPU Viewer (Offset X: {offsets[0]:.4f}, Y: {offsets[1]:.4f})")
        ax.set_xlabel("X (Centered)")
        ax.set_ylabel("Y (Centered)")
        ax.grid(True, linestyle='--', alpha=0.5)
        ax.set_aspect('equal', 'box') # 锁定宽高比为 1:1

    elif num_cols >= 3:
        # ==================== 3D 纯 CPU 离线渲染 ====================
        ax = fig.add_subplot(111, projection='3d')
        ax.scatter(data_rendered[:, 0], data_rendered[:, 1], data_rendered[:, 2], color='#FF5050', s=4, alpha=0.8)

        ax.set_title("3D CPU Viewer (Centered)")
        ax.set_xlabel("X (Centered)")
        ax.set_ylabel("Y (Centered)")
        ax.set_zlabel("Z (Centered)")
        ax.grid(True, linestyle='--', alpha=0.5)

        try:
            ax.set_box_aspect([1.0, 1.0, 1.0])
        except AttributeError:
            pass

    else:
        print("不支持的数据维度。")
        return

    # 💡 核心改变：不再弹窗（防止崩溃），直接在后台渲染并保存为图片
    output_image = "point_cloud_render.png"
    print(f"正在后台渲染并保存图片至: {output_image} ...")
    plt.savefig(output_image, dpi=300, bbox_inches='tight')
    plt.close(fig)
    print("渲染保存完毕。")

if __name__ == '__main__':
    main()