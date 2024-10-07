#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <ctime>
#include <fstream>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <sys/types.h>

constexpr int NUM_CAMERAS = 6;
constexpr int FRAME_WIDTH = 1280;
constexpr int FRAME_HEIGHT = 720;

// 全局变量
std::vector<void*> buffer_start(NUM_CAMERAS);
std::vector<int> fds(NUM_CAMERAS);
std::vector<struct v4l2_buffer> buffers(NUM_CAMERAS);
std::atomic<bool> capture_images(false);
std::vector<std::atomic<bool>> camera_saved(NUM_CAMERAS);
std::atomic<bool> exit_program(false);

// 实现信号量类
class Semaphore {
public:
    Semaphore(int count = 0)
        : count(count) {}

    void notify() {
        std::unique_lock<std::mutex> lock(mtx);
        ++count;
        cv.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return count > 0; });
        --count;
    }

private:
    std::mutex mtx;
    std::condition_variable cv;
    int count;
};

// 定义一个信号量，初始值为5，限制同时更新的相机数量
Semaphore camera_semaphore(5);

// 创建目录的函数
void create_directory(const std::string& folder_name) {
    struct stat info;
    if (stat(folder_name.c_str(), &info) != 0) {
        // 如果目录不存在，创建目录
        if (mkdir(folder_name.c_str(), 0777) == -1) {
            std::cerr << "创建目录失败：" << folder_name << " - " << strerror(errno) << std::endl;
        }
    }
}

// 相机采集函数
void capture_camera(int camera_id) {
    std::string device = "/dev/video" + std::to_string(camera_id * 2);

    // 打开相机设备
    fds[camera_id] = open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (fds[camera_id] == -1) {
        std::cerr << "无法打开设备：" << device << " - " << strerror(errno) << std::endl;
        return;
    }

    // 查询设备能力
    struct v4l2_capability cap;
    if (ioctl(fds[camera_id], VIDIOC_QUERYCAP, &cap) == -1) {
        std::cerr << "查询设备能力失败：" << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // 设置视频格式
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = FRAME_WIDTH;
    fmt.fmt.pix.height = FRAME_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fds[camera_id], VIDIOC_S_FMT, &fmt) == -1) {
        std::cerr << "设置视频格式失败：" << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // 请求缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fds[camera_id], VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "请求缓冲区失败：" << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // 映射缓冲区
    struct v4l2_buffer &buf = buffers[camera_id];
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fds[camera_id], VIDIOC_QUERYBUF, &buf) == -1) {
        std::cerr << "查询缓冲区失败：" << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    buffer_start[camera_id] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fds[camera_id], buf.m.offset);
    if (buffer_start[camera_id] == MAP_FAILED) {
        std::cerr << "内存映射失败：" << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // 启动视频流
    if (ioctl(fds[camera_id], VIDIOC_STREAMON, &buf.type) == -1) {
        std::cerr << "启动视频流失败：" << device << " - " << strerror(errno) << std::endl;
        munmap(buffer_start[camera_id], buf.length);
        close(fds[camera_id]);
        return;
    }

    // 将缓冲区放入队列
    if (ioctl(fds[camera_id], VIDIOC_QBUF, &buf) == -1) {
        std::cerr << "缓冲区入队失败：" << device << " - " << strerror(errno) << std::endl;
        munmap(buffer_start[camera_id], buf.length);
        close(fds[camera_id]);
        return;
    }

    fd_set fds_set;
    struct timeval tv;

    while (!exit_program.load()) {
        auto start_time = std::chrono::high_resolution_clock::now();

        // 等待信号量，确保同时只有5个相机在更新数据
        camera_semaphore.wait();

        FD_ZERO(&fds_set);
        FD_SET(fds[camera_id], &fds_set);

        tv.tv_sec = 1;  // 设置select超时时间为1秒
        tv.tv_usec = 0;

        int r = select(fds[camera_id] + 1, &fds_set, NULL, NULL, &tv);
        if (r == -1) {
            std::cerr << "select错误：" << strerror(errno) << std::endl;
            // 释放信号量
            camera_semaphore.notify();
            break;
        } else if (r == 0) {
            std::cerr << "相机 " << camera_id << " select超时。" << std::endl;
            // 释放信号量
            camera_semaphore.notify();
            continue;
        }

        // 从队列中取出缓冲区
        if (ioctl(fds[camera_id], VIDIOC_DQBUF, &buf) == -1) {
            std::cerr << "缓冲区出队失败：" << device << " - " << strerror(errno) << std::endl;
            // 释放信号量
            camera_semaphore.notify();
            break;
        }

        // 如果需要显示第一个相机的画面
        if (camera_id == 0) {
            cv::Mat yuyv(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC2, buffer_start[camera_id]);
            cv::Mat bgr;
            cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
            cv::Mat resized_bgr;
            cv::resize(bgr, resized_bgr, cv::Size(640, 480));
            cv::imshow("Video0 Live Feed", resized_bgr);

            if (cv::waitKey(1) == 'q') {
                exit_program = true;
                // 释放信号量
                camera_semaphore.notify();
                break;
            }
        }

        // 检查是否需要保存图像
        if (capture_images.load() && !camera_saved[camera_id].load()) {
            // 创建目录
            std::string folder_name = "data";
            create_directory(folder_name);

            // 保存当前帧为 .yuyv
            std::string filename = folder_name + "/camera_" + std::to_string(camera_id) + "_" + std::to_string(std::time(nullptr)) + ".yuyv";
            std::ofstream out_file(filename, std::ios::binary);
            out_file.write(reinterpret_cast<char*>(buffer_start[camera_id]), buf.bytesused);
            std::cout << "保存了相机 " << camera_id << " 的图像：" << filename << std::endl;

            // 设置已保存标志
            camera_saved[camera_id] = true;
        }

        // 将缓冲区重新放入队列
        if (ioctl(fds[camera_id], VIDIOC_QBUF, &buf) == -1) {
            std::cerr << "缓冲区重新入队失败：" << device << " - " << strerror(errno) << std::endl;
            // 释放信号量
            camera_semaphore.notify();
            break;
        }

        // 释放信号量，允许其他相机更新数据
        camera_semaphore.notify();

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::milliseconds frame_duration(30);
        std::this_thread::sleep_until(start_time + frame_duration);
    }

    // 停止视频流
    if (ioctl(fds[camera_id], VIDIOC_STREAMOFF, &buf.type) == -1) {
        std::cerr << "停止视频流失败：" << device << " - " << strerror(errno) << std::endl;
    }

    // 释放资源
    munmap(buffer_start[camera_id], buf.length);
    close(fds[camera_id]);
}

// 键盘监听线程
void keyboard_listener() {
    while (!exit_program.load()) {
        char key = std::cin.get();
        if (key == 's') {
            // 重置camera_saved标志
            for (int i = 0; i < NUM_CAMERAS; ++i) {
                camera_saved[i] = false;
            }

            // 设置capture_images为true
            capture_images = true;

            // 等待所有相机完成保存
            bool all_saved = false;
            while (!all_saved) {
                all_saved = true;
                for (int i = 0; i < NUM_CAMERAS; ++i) {
                    if (!camera_saved[i].load()) {
                        all_saved = false;
                        break;
                    }
                }
                if (!all_saved) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

            // 重置capture_images
            capture_images = false;
        } else if (key == 'q') {
            exit_program = true;
            break;
        }
    }
}

int main() {
    // 初始化camera_saved标志
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        camera_saved[i] = false;
    }

    // 启动相机线程
    std::vector<std::thread> camera_threads;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        camera_threads.emplace_back(capture_camera, i);
    }

    // 启动键盘监听线程
    std::thread listener_thread(keyboard_listener);

    // 等待所有线程完成
    for (auto &t : camera_threads) {
        t.join();
    }

    listener_thread.join();

    cv::destroyAllWindows();

    return 0;
}
