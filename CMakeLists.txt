cmake_minimum_required(VERSION 3.20)
project(iocpnet VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准为C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加iocpnet库子目录
add_subdirectory(iocpnet)

# 添加示例子目录
add_subdirectory(examples)