cmake_minimum_required(VERSION 3.16)
project(rsm_baseline LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SOC_VERSION "Ascend910B3" CACHE STRING "Ascend SoC")
set(ASCEND_CANN_PACKAGE_PATH "$ENV{ASCEND_HOME_PATH}" CACHE PATH "CANN root")
set(RUN_MODE "npu" CACHE STRING "Ascend C run mode")

if(EXISTS "${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake")
  set(ASCENDC_CMAKE_DIR "${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake")
elseif(EXISTS "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake")
  set(ASCENDC_CMAKE_DIR "${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake")
else()
  message(FATAL_ERROR "ascendc_kernel_cmake was not found below ${ASCEND_CANN_PACKAGE_PATH}")
endif()

include("${ASCENDC_CMAKE_DIR}/ascendc.cmake")
ascendc_library(rsm_baseline_kernels SHARED op_kernel/rsm_baseline.cpp)

add_executable(rsm_baseline_runner runner/main.cpp op_host/rsm_baseline_host.cpp)
add_dependencies(rsm_baseline_runner rsm_baseline_kernels)
target_include_directories(rsm_baseline_runner PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/op_kernel"
  "${CMAKE_CURRENT_SOURCE_DIR}/op_host"
  "${CMAKE_CURRENT_BINARY_DIR}/include/rsm_baseline_kernels"
  "${ASCEND_CANN_PACKAGE_PATH}/include")
target_link_directories(rsm_baseline_runner PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64")
target_link_libraries(rsm_baseline_runner PRIVATE rsm_baseline_kernels ascendcl)
install(TARGETS rsm_baseline_runner RUNTIME DESTINATION bin)
