set(pro Rock)

add_executable(${pro}
        gravity_sim.cpp
        Octree.cpp
        Scene.cpp
        )

# 主程序依赖的库基本覆盖三类职责：
# 1. glfw/glad: 创建窗口和加载 OpenGL API；
# 2. glm: 数学库；
# 3. imgui/assimp/stb/tinyobjloader: UI 与资源支持。
target_link_libraries(${pro} PRIVATE assimp::assimp  glm stb glfw tinyobjloader glad imgui)
target_include_directories(${pro} PRIVATE
        ../third_party/glew/include
        ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(${pro} PRIVATE cxx_std_17)

# 通过 configure_file 生成 config.h，便于以后从 CMake 注入宏配置。
configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_custom_command(TARGET ${pro} POST_BUILD
    # 把 shader 复制到 build 目录，确保可执行文件在构建目录运行时也能找到资源。
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)
