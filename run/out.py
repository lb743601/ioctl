import cv2
import numpy as np
import os

# 读取YUYV图像数据
def read_yuyv_image(file_path, width, height):
    with open(file_path, 'rb') as f:
        yuyv_data = f.read()
    yuyv_array = np.frombuffer(yuyv_data, dtype=np.uint8)
    yuyv_image = yuyv_array.reshape((height, width, 2))
    return yuyv_image

# 将YUYV图像转换为BGR格式
def yuyv_to_bgr(yuyv_image):
    bgr_image = cv2.cvtColor(yuyv_image, cv2.COLOR_YUV2BGR_YUYV)
    return bgr_image

# 保存为JPEG格式
def save_as_jpeg(image, output_path):
    cv2.imwrite(output_path, image)

# 主程序
if __name__ == "__main__":
    # YUYV图像的宽度和高度（根据实际情况修改）
    width = 1280
    height = 720

    # 输入和输出文件夹路径
    input_folder = 'data'
    output_folder = 'output'
    os.makedirs(output_folder, exist_ok=True)  # 如果output文件夹不存在则创建

    # 遍历data文件夹中的所有YUYV文件
    for filename in os.listdir(input_folder):
        if filename.endswith('.yuyv'):
            yuyv_file_path = os.path.join(input_folder, filename)
            # 生成JPEG文件名
            jpeg_filename = os.path.splitext(filename)[0] + '.jpg'
            jpeg_output_path = os.path.join(output_folder, jpeg_filename)
            
            # 读取并转换YUYV图像
            yuyv_image = read_yuyv_image(yuyv_file_path, width, height)
            bgr_image = yuyv_to_bgr(yuyv_image)
            
            # 保存JPEG图像
            save_as_jpeg(bgr_image, jpeg_output_path)
            
            print(f"已成功将 {filename} 转换为 {jpeg_filename} 并保存到 {output_folder} 文件夹中")
    
    print("所有YUYV图像已成功转换并保存。")
