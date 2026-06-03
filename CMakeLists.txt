cmake_minimum_required(VERSION 3.14)
project(PathTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# OpenMP
find_package(OpenMP REQUIRED)

add_executable(pathtracer
    src/main.cpp
    src/core/Material.cpp
    src/scene/SceneLoading.cpp
    src/environment/Sky.cpp
    src/core/Renderer.cpp
    src/scene/Scene.cpp
    src/core/BVH.cpp
    src/scene/Texture.cpp
    src/scene/JsonParser.cpp
    src/environment/Ocean.cpp
)

target_include_directories(pathtracer PRIVATE include)
target_link_libraries(pathtracer PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(pathtracer PRIVATE -O2 -Wall)
