import numpy as np
from PIL import Image

# 图片分辨率 (1920x1080 级别的浅灰色微弱渐变)
width = 1920
height = 1080

# -------------------------------------------------------------
# 1. 生成 8-bit 渐变图 (只有 5 个灰度级阶梯: 120 到 125)
# -------------------------------------------------------------
grad_8 = np.linspace(120, 125, width, dtype=np.uint8)
img_data_8 = np.tile(grad_8, (height, 1))
img_8 = Image.fromarray(img_data_8, mode='L')
img_8.save('gradient_8bit.png')
print("Successfully generated 8-bit gradient (gradient_8bit.png)")

# -------------------------------------------------------------
# 2. 生成 10-bit 级别的渐变图 (利用 16-bit 格式存储，台阶数扩大 256 倍)
# -------------------------------------------------------------
start_16 = 120 * 256 # 映射到 16 位的起始值
end_16 = 125 * 256   # 映射到 16 位的结束值

grad_16 = np.linspace(start_16, end_16, width, dtype=np.uint16)
img_data_16 = np.tile(grad_16, (height, 1))

# Pillow 支持 'I;16' 模式保存标准的 16-bit 灰度 PNG
img_16 = Image.fromarray(img_data_16, mode='I;16')
img_16.save('gradient_16bit.png')
print("Successfully generated 10-bit/16-bit gradient (gradient_16bit.png)")