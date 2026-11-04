cmake_minimum_required(VERSION 3.20)
project(indicode_package_consumer LANGUAGES CXX)

find_package(indicode 1.1.0 EXACT CONFIG REQUIRED)

add_executable(frontend_consumer main.cpp)
target_link_libraries(frontend_consumer PRIVATE indicode::frontend)
