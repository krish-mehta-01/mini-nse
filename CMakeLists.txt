cmake_minimum_required(VERSION 3.22)
project(mini_nse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets VS Code understand includes and flags

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)  # use -DCMAKE_BUILD_TYPE=Release when measuring speed
endif()

# GoogleTest, downloaded the first time you configure
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# Warnings apply to our code only (added after GoogleTest on purpose)
add_compile_options(-Wall -Wextra -Wpedantic)

enable_testing()
include(GoogleTest)

# Phase 1: add your engine as a library here (add_library) and link it into the tests below.

add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE GTest::gtest_main)
gtest_discover_tests(smoke_test)
