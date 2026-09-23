cmake_minimum_required(VERSION 3.20)
project(MaiMoe VERSION 0.2.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_EXTENSIONS OFF)

function(maimoe_configure_kernel_target target)
    target_link_libraries(${target} PRIVATE maimoe_engine Threads::Threads)
    target_include_directories(${target} PRIVATE src/worker)
    target_compile_features(${target} PRIVATE cxx_std_20)
endfunction()

add_library(maimoe_engine
    src/sha256.cpp
    src/manifest.cpp
    src/simai.cpp
    src/state.cpp
    src/render.cpp
    src/MaiMoeEngine.cpp
)
target_include_directories(maimoe_engine PUBLIC include src)
target_compile_features(maimoe_engine PUBLIC cxx_std_20)

add_executable(maimoe
    src/main.cpp
    src/host/HostRuntime.cpp
    src/host/Schedule.cpp
    src/host/TuneConfig.cpp
    src/host/WorkerProcess.cpp
)
target_link_libraries(maimoe PRIVATE maimoe_engine Threads::Threads)
target_include_directories(maimoe PRIVATE src/host src/worker)
target_compile_features(maimoe PRIVATE cxx_std_20)

add_executable(maimoe_kernel
    src/worker/WorkerMain.cpp
    solution/Kernel.cpp
)
maimoe_configure_kernel_target(maimoe_kernel)
add_dependencies(maimoe maimoe_kernel)

add_executable(maimoe_check src/judge/check.cpp)
target_link_libraries(maimoe_check PRIVATE maimoe_engine)
target_compile_features(maimoe_check PRIVATE cxx_std_20)
