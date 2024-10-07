cmake_minimum_required(VERSION 3.10)
project(multi_camera_capture)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找V4L2库
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
pkg_check_modules(V4L2 REQUIRED libv4l2)

# 添加可执行文件
add_executable(multi_camera_capture main.cpp)

# 链接V4L2库
target_include_directories(multi_camera_capture PRIVATE ${V4L2_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(multi_camera_capture ${V4L2_LIBRARIES} pthread ${OpenCV_LIBRARIES})
