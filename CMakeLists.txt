cmake_minimum_required(VERSION 3.25)

project(k8s-operator-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(K8SOP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(K8SOP_CLANG_TIDY "Run clang-tidy during the build" OFF)
set(K8SOP_SANITIZER "" CACHE STRING "Sanitizers to enable, e.g. 'address,undefined' or 'thread'")

include(cmake/ProjectOptions.cmake)

find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

# Core library: everything except main() lives here so tests and benchmarks can link it.
add_library(k8sop
  src/version.cpp
  src/api/meta.cpp
  src/api/pod.cpp
)
target_include_directories(k8sop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(k8sop PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json)
k8sop_configure_target(k8sop)

add_executable(operator src/main.cpp)
target_link_libraries(operator PRIVATE k8sop)
k8sop_configure_target(operator)

enable_testing()
include(GoogleTest)

add_executable(tests
  tests/version_test.cpp
  tests/api/meta_test.cpp
  tests/api/pod_test.cpp
)
target_link_libraries(tests PRIVATE k8sop GTest::gtest_main)
k8sop_configure_target(tests)
gtest_discover_tests(tests)

add_executable(bench
  bench/string_bench.cpp
)
target_link_libraries(bench PRIVATE k8sop benchmark::benchmark_main)
k8sop_configure_target(bench)
