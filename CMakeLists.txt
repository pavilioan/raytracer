cmake_minimum_required(VERSION 3.10)
project(RayTracer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(raytracer src/main.cpp)
target_include_directories(raytracer PUBLIC include)
target_link_libraries(raytracer Threads::Threads)
