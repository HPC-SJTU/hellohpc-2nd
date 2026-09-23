cmake_minimum_required(VERSION 3.22)

project(full_analyze_interactor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
)

add_library(interactor_lib src/interactor.cpp)
target_include_directories(interactor_lib PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(interactor_lib PUBLIC Threads::Threads)

add_executable(interactor src/main.cpp)
target_link_libraries(interactor PRIVATE interactor_lib)

include(CTest)
if(BUILD_TESTING)
  add_executable(fixture_child tests/fixture_child.cpp)
  add_executable(interactor_test tests/interactor_test.cpp)
  target_link_libraries(interactor_test PRIVATE interactor_lib Threads::Threads)
  add_test(NAME interactor_test COMMAND interactor_test $<TARGET_FILE:fixture_child>)
endif()
