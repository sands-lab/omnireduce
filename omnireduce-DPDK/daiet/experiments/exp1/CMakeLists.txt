project(switchml)
cmake_minimum_required(VERSION 3.5)

set(USE_VANILLA_DEFAULT OFF)
set(USE_MLX5_DEFAULT OFF)
set(USE_MLX4_DEFAULT OFF)
set(DEBUG_DEFAULT OFF)

# Options
option(USE_VANILLA "Use vanilla version of gloo" ${USE_VANILLA_DEFAULT})
option(USE_MLX5 "Use MLX5 and ibverbs" ${USE_MLX5_DEFAULT})
option(USE_MLX4 "Use MLX4 and ibverbs" ${USE_MLX4_DEFAULT})
option(DEBUG "Compile in debug mode" ${DEBUG_DEFAULT})

if(DEBUG)
    message(WARNING "Compile in debug mode")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -g -O0")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fPIC -O3 -DNDEBUG")
endif()

add_definitions("-DHIREDIS_NESTED_INCLUDE")

link_directories(${CMAKE_SOURCE_DIR}/../../../gloo/build/gloo)
include_directories(${CMAKE_SOURCE_DIR}/../../../gloo/build)
include_directories(${CMAKE_SOURCE_DIR}/../../../gloo)

if(NOT USE_VANILLA)
    include_directories(${CMAKE_SOURCE_DIR}/../../build/include)
    link_directories(${CMAKE_SOURCE_DIR}/../../build)
    link_directories(${CMAKE_SOURCE_DIR}/../../lib/dpdk/build/lib)
else()
    message(WARNING "Compiling with vanilla gloo")
endif()

add_executable(exp1 main.cc)

if(NOT USE_VANILLA)
    target_link_libraries(exp1 -Wl,--whole-archive daiet dpdk -Wl,--no-whole-archive dl numa boost_chrono boost_system boost_thread boost_program_options)
endif()

if(USE_MLX5)
    target_link_libraries(exp1 ibverbs mlx5 mnl)
endif()

if(USE_MLX4)
    target_link_libraries(exp1 ibverbs mlx4 mnl)
endif()

target_link_libraries(exp1 gloo hiredis pthread)
