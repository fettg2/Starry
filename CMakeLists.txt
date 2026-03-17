cmake_minimum_required(VERSION 3.15)
# 顶层 CMake 只负责把源码目录和第三方依赖目录组织起来。
project(Hand)
add_subdirectory(src)
add_subdirectory(third_party)
