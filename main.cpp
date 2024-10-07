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
#include <opencv2/opencv.hpp>  // Include OpenCV headers
#include <sys/stat.h>
#include <sys/types.h>
constexpr int NUM_CAMERAS = 6;
constexpr int FRAME_WIDTH = 1280;
constexpr int FRAME_HEIGHT = 720;

std::vector<void*> buffer_start(NUM_CAMERAS);
std::vector<int> fds(NUM_CAMERAS);
std::vector<struct v4l2_buffer> buffers(NUM_CAMERAS);
std::atomic<bool> capture_images(false);
std::vector<std::atomic<bool>> camera_saved(NUM_CAMERAS);
std::atomic<bool> exit_program(false);  // Flag to exit the program gracefully


void create_directory(const std::string& folder_name) {
    struct stat info;
    if (stat(folder_name.c_str(), &info) != 0) {
        // 文件夹不存在，则创建
        if (mkdir(folder_name.c_str(), 0777) == -1) {
            std::cerr << "Error creating directory " << folder_name << " - " << strerror(errno) << std::endl;
        }
    }
}

void capture_camera(int camera_id) {
    std::string device = "/dev/video" + std::to_string(camera_id * 2);

    // Open camera device
    fds[camera_id] = open(device.c_str(), O_RDWR);
    if (fds[camera_id] == -1) {
        std::cerr << "Cannot open device: " << device << " - " << strerror(errno) << std::endl;
        return;
    }

    // Query device capabilities
    struct v4l2_capability cap;
    if (ioctl(fds[camera_id], VIDIOC_QUERYCAP, &cap) == -1) {
        std::cerr << "Failed to query device capabilities: " << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // Set video format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = FRAME_WIDTH;
    fmt.fmt.pix.height = FRAME_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fds[camera_id], VIDIOC_S_FMT, &fmt) == -1) {
        std::cerr << "Failed to set video format: " << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // Request buffer
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fds[camera_id], VIDIOC_REQBUFS, &req) == -1) {
        std::cerr << "Failed to request buffer: " << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // Map buffer
    struct v4l2_buffer &buf = buffers[camera_id];
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(fds[camera_id], VIDIOC_QUERYBUF, &buf) == -1) {
        std::cerr << "Failed to query buffer: " << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    buffer_start[camera_id] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fds[camera_id], buf.m.offset);
    if (buffer_start[camera_id] == MAP_FAILED) {
        std::cerr << "Memory mapping failed: " << device << " - " << strerror(errno) << std::endl;
        close(fds[camera_id]);
        return;
    }

    // Start video stream
    if (ioctl(fds[camera_id], VIDIOC_STREAMON, &buf.type) == -1) {
        std::cerr << "Failed to start video stream: " << device << " - " << strerror(errno) << std::endl;
        munmap(buffer_start[camera_id], buf.length);
        close(fds[camera_id]);
        return;
    }

    while (!exit_program.load()) {
        auto start_time = std::chrono::high_resolution_clock::now();
        // Enqueue buffer
        if (ioctl(fds[camera_id], VIDIOC_QBUF, &buf) == -1) {
            std::cerr << "Failed to enqueue buffer: " << device << " - " << strerror(errno) << std::endl;
            break;
        }

        // Dequeue buffer
        if (ioctl(fds[camera_id], VIDIOC_DQBUF, &buf) == -1) {
            std::cerr << "Failed to dequeue buffer: " << device << " - " << strerror(errno) << std::endl;
            break;
        }

        // If camera_id == 0, display the frame
        if (camera_id == 0) {
            // Convert YUYV to BGR for display
            cv::Mat yuyv(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC2, buffer_start[camera_id]);
            cv::Mat bgr;
            cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
            cv::Mat resized_bgr;
            cv::resize(bgr, resized_bgr, cv::Size(640, 480));
            cv::imshow("Video0 Live Feed", resized_bgr);

            // Check for 'q' key press
            if (cv::waitKey(1) == 'q') {
                exit_program = true;
                break;
            }
        }

        // Check if we need to save the image
        if (capture_images.load() && !camera_saved[camera_id].load()) {
            // 创建文件夹（如果不存在）
            std::string folder_name = "data";
            create_directory(folder_name);

            // 生成文件路径，并保存当前帧为 .yuyv
            std::string filename = folder_name + "/camera_" + std::to_string(camera_id) + "_" + std::to_string(std::time(nullptr)) + ".yuyv";
            std::ofstream out_file(filename, std::ios::binary);
            out_file.write(static_cast<char*>(buffer_start[camera_id]), buf.bytesused);
            std::cout << "Saved image from camera " << camera_id << " as: " << filename << std::endl;

            // 设置camera_saved标志
            camera_saved[camera_id] = true;
            }
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::milliseconds frame_duration(33);  // 30 FPS -> 每帧约 33ms
    std::this_thread::sleep_until(start_time + frame_duration);
    }

    // Stop video stream
    if (ioctl(fds[camera_id], VIDIOC_STREAMOFF, &buf.type) == -1) {
        std::cerr << "Failed to stop video stream: " << device << " - " << strerror(errno) << std::endl;
    }

    // Release resources
    munmap(buffer_start[camera_id], buf.length);
    close(fds[camera_id]);
}

void keyboard_listener() {
    while (!exit_program.load()) {
        char key = std::cin.get();
        if (key == 's') {
            // Reset camera_saved flags
            for (int i = 0; i < NUM_CAMERAS; ++i) {
                camera_saved[i] = false;
            }

            // Set capture_images to true
            capture_images = true;

            // Wait until all cameras have saved images
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

            // Reset capture_images for next capture
            capture_images = false;
        } else if (key == 'q') {
            // Exit the program gracefully
            exit_program = true;
            break;
        }
    }
}

int main() {
    // Initialize camera_saved flags
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        camera_saved[i] = false;
    }

    // Start camera threads
    std::vector<std::thread> camera_threads;
    for (int i = 0; i < NUM_CAMERAS; ++i) {
        camera_threads.emplace_back(capture_camera, i);
    }

    std::thread listener_thread(keyboard_listener);

    // Wait for all threads to complete
    for (auto &t : camera_threads) {
        t.join();
    }

    listener_thread.join();

    cv::destroyAllWindows();

    return 0;
}
