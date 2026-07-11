cmake_minimum_required(VERSION 3.16)

project(uav_smoke_dji_adapter LANGUAGES CXX)

enable_testing()

add_library(uav_smoke_dji_adapter STATIC
    src/mission_planner.cpp
    src/mission_plan_writer.cpp
    src/mock_telemetry_source.cpp
    src/mock_video_source.cpp
    src/smoke_event_builder.cpp
    src/target_geolocation.cpp
)

target_include_directories(uav_smoke_dji_adapter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(uav_smoke_dji_adapter
    PUBLIC
        cxx_std_11
)

add_executable(mock_adapter_demo
    src/mock_adapter_demo.cpp
)

target_link_libraries(mock_adapter_demo
    PRIVATE
        uav_smoke_dji_adapter
)

add_executable(smoke_mission_demo
    demos/smoke_mission_demo.cpp
)

target_link_libraries(smoke_mission_demo
    PRIVATE
        uav_smoke_dji_adapter
)

add_executable(target_geolocation_test
    tests/target_geolocation_test.cpp
)

target_link_libraries(target_geolocation_test
    PRIVATE
        uav_smoke_dji_adapter
)

add_test(NAME target_geolocation_test COMMAND target_geolocation_test)

add_executable(smoke_event_builder_test
    tests/smoke_event_builder_test.cpp
)

target_link_libraries(smoke_event_builder_test
    PRIVATE
        uav_smoke_dji_adapter
)

add_test(NAME smoke_event_builder_test COMMAND smoke_event_builder_test)

add_executable(mission_planner_test
    tests/mission_planner_test.cpp
)

target_link_libraries(mission_planner_test
    PRIVATE
        uav_smoke_dji_adapter
)

add_test(NAME mission_planner_test COMMAND mission_planner_test)

add_executable(mission_plan_writer_test
    tests/mission_plan_writer_test.cpp
)

target_link_libraries(mission_plan_writer_test
    PRIVATE
        uav_smoke_dji_adapter
)

add_test(NAME mission_plan_writer_test COMMAND mission_plan_writer_test)
