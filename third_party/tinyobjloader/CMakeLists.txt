add_library(tinyobjloader STATIC 
    tiny_obj_loader.h
    tiny_obj_loader.cc
)

target_compile_features(tinyobjloader PRIVATE cxx_std_11)

target_include_directories(tinyobjloader PUBLIC .)