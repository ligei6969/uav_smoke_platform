cmake_minimum_required(VERSION 3.16)

project(uav_smoke_dji_adapter LANGUAGES CXX)

add_library(uav_smoke_dji_adapter INTERFACE)

target_include_directories(uav_smoke_dji_adapter
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(uav_smoke_dji_adapter
    INTERFACE
        cxx_std_11
)
