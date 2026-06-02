cmake_minimum_required(VERSION 3.14)
project(PathTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# OpenMP
find_package(OpenMP REQUIRED)

add_executable(pathtracer
    src/main.cpp
    src/Material.cpp
    src/MtlConverter.cpp
    src/MeshUtils.cpp
    src/SceneLoading.cpp
    src/Diagnostics.cpp
    src/Sky.cpp
    src/AircraftShaders.cpp
    src/Renderer.cpp
    src/Scene.cpp
    src/BVH.cpp
    src/Texture.cpp
    src/JsonParser.cpp
    src/Ocean.cpp
)

target_include_directories(pathtracer PRIVATE include)
target_link_libraries(pathtracer PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(pathtracer PRIVATE -O2 -Wall)
