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

# The engine, as a library so tests (and later the replay tool) can share it
add_library(mini_nse
  src/auction_book.cpp
  src/auction.cpp
  src/order_book.cpp
  src/price.cpp
  src/script.cpp
  src/session.cpp
)
target_include_directories(mini_nse PUBLIC include)

add_executable(mini_nse_tests
  tests/auction_book_test.cpp
  tests/auction_test.cpp
  tests/auction_fill_test.cpp
  tests/order_book_test.cpp
  tests/script_test.cpp
  tests/session_test.cpp
)
target_link_libraries(mini_nse_tests PRIVATE mini_nse GTest::gtest_main)
gtest_discover_tests(mini_nse_tests)

# Command-line replay: plays morning files through the engine
add_executable(mini_nse_replay apps/replay.cpp)
target_link_libraries(mini_nse_replay PRIVATE mini_nse)

# Golden-file test: the replay of the example morning must not change
add_test(NAME replay_case1_morning_matches_golden_file
  COMMAND sh -c "$<TARGET_FILE:mini_nse_replay> examples/case1_morning.txt | diff - examples/case1_morning.expected"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Random cross-check against the independent Python reference (tools/fuzz.py runs bigger batches)
add_test(NAME fuzz_300_random_mornings_against_python_reference
  COMMAND python3 tools/fuzz.py --replay $<TARGET_FILE:mini_nse_replay> --count 300 --seed 7
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
