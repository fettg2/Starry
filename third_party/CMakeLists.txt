cmake_policy(SET CMP0077 NEW)
set(ASSIMP_NO_EXPORT ON)
set(BUILD_SHARED_LIBS OFF)


add_subdirectory(assimp)
# add_subdirectory(glew)
add_subdirectory(glm)
add_subdirectory(stb)
add_subdirectory(glfw)
add_subdirectory(tinyobjloader)

add_library(glad STATIC glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)

set(IMGUI_DIR "imgui") 


set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/imgui_impl_glfw.cpp
)


add_library(imgui STATIC ${IMGUI_SOURCES})


target_include_directories(imgui PUBLIC 
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)


target_link_libraries(imgui PRIVATE glfw glad)