cmake_minimum_required(VERSION 3.20)
project(apex LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(apex app/main.cpp)

# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_executable(orderbook_test tests/orderbook_test.cpp)
target_include_directories(orderbook_test PRIVATE include)
target_link_libraries(orderbook_test GTest::gtest_main)
add_test(NAME orderbook_test COMMAND orderbook_test)

add_executable(marketmaker_test tests/marketmaker_test.cpp)
target_include_directories(marketmaker_test PRIVATE include)
target_link_libraries(marketmaker_test GTest::gtest_main)
add_test(NAME marketmaker_test COMMAND marketmaker_test)
