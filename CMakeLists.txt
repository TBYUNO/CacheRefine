# CMake最低版本要求
cmake_minimum_required(VERSION 3.10.0)
# 项目名，版本与语言
project(CacheManager VERSION 0.1.0 LANGUAGES C CXX)

# C++版本要求
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# 添加主程序（测试文件）
add_executable(CacheSystem main.cpp)

# 包含 src 头文件
target_include_directories(CacheSystem PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Utility
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LRU
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LFU
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ARC
)