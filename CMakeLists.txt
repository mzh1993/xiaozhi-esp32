# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.6.0")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# 这行代码是必需的,它包含了ESP-IDF的核心构建系统配置
# 必须在project()命令之前包含这个文件
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# xiaozhi是项目名称,表示"小智"AI聊天机器人
# 这个名称会用作编译输出的二进制文件名和其他构建系统标识
project(xiaozhi)

