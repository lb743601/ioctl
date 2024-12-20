import cv2
import time
import os
from datetime import datetime

class MultiCameraCapture:
    def __init__(self):
        # 存储6个相机的设备索引
        self.camera_indices = [0, 2, 4, 6, 8, 10]  # 偶数索引
        self.preview_index = 0  # 当前预览的相机索引
        self.cap = None
        self.window_name = 'Camera Preview'

    def open_camera(self, index):
        """打开指定索引的相机"""
        if self.cap is not None:
            self.cap.release()
        
        self.cap = cv2.VideoCapture(index)
        if not self.cap.isOpened():
            raise Exception(f"无法打开相机 /dev/video{index}")
        
        # 设置相机参数（根据需要调整）
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    def capture_all_cameras(self):
        """依次从所有相机捕获图像"""
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        images = []
        
        # 创建保存图像的文件夹
        save_dir = f"captured_images_{timestamp}"
        os.makedirs(save_dir, exist_ok=True)
        
        for idx in self.camera_indices:
            try:
                # 打开当前相机
                self.open_camera(idx)
                # 等待相机稳定
                time.sleep(0.01)
                
                # 捕获几帧以确保图像质量（丢弃前几帧）
                # for _ in range(5):
                #     ret, frame = self.cap.read()
                
                # 捕获最终图像
                ret, frame = self.cap.read()
                if ret:
                    # 保存图像
                    filename = os.path.join(save_dir, f"camera_{idx}_{timestamp}.jpg")
                    cv2.imwrite(filename, frame)
                    images.append(frame)
                    print(f"已保存相机 {idx} 的图像到 {filename}")
                else:
                    print(f"无法从相机 {idx} 捕获图像")
                
            except Exception as e:
                print(f"处理相机 {idx} 时出错: {str(e)}")
            finally:
                if self.cap is not None:
                    self.cap.release()
        
        return images

    def preview_loop(self):
        """预览循环"""
        try:
            # 初始化预览第一个相机
            self.open_camera(self.camera_indices[self.preview_index])
            cv2.namedWindow(self.window_name)
            
            while True:
                ret, frame = self.cap.read()
                if ret:
                    # 显示当前相机索引
                    cv2.putText(frame, f"Camera {self.camera_indices[self.preview_index]}", 
                              (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                    cv2.imshow(self.window_name, frame)
                
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    break
                elif key == ord('s'):
                    print("开始捕获所有相机图像...")
                    self.capture_all_cameras()
                    # 重新打开预览相机
                    self.open_camera(self.camera_indices[self.preview_index])
                elif key == ord('n'):
                    # 切换到下一个相机预览
                    self.preview_index = (self.preview_index + 1) % len(self.camera_indices)
                    self.open_camera(self.camera_indices[self.preview_index])
                
        finally:
            if self.cap is not None:
                self.cap.release()
            cv2.destroyAllWindows()

    def run(self):
        """运行程序"""
        print("相机预览程序已启动")
        print("按 'n' 切换预览的相机")
        print("按 's' 保存所有相机的图像")
        print("按 'q' 退出程序")
        self.preview_loop()

if __name__ == "__main__":
    capture_system = MultiCameraCapture()
    capture_system.run()