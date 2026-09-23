cmake_minimum_required(VERSION 3.22)

project(full_analyze_checker LANGUAGES CXX)

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

add_library(checker_lib src/checker.cpp)
target_include_directories(checker_lib PUBLIC src)

add_executable(checker src/main.cpp)
target_link_libraries(checker PRIVATE checker_lib)

include(CTest)
if(BUILD_TESTING)
  add_executable(checker_test tests/checker_test.cpp)
  target_link_libraries(checker_test PRIVATE checker_lib)
  add_test(NAME checker_test COMMAND checker_test)
endif()
