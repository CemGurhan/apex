cmake_minimum_required(VERSION 3.20)
project(apex LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(apex app/main.cpp app/sim/sim.cpp)
target_include_directories(apex PRIVATE include app)

if(APPLE)
    add_custom_command(TARGET apex POST_BUILD
        COMMAND codesign -s - --entitlements ${CMAKE_SOURCE_DIR}/debug.entitlements -f $<TARGET_FILE:apex>
    )
endif()

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

add_executable(ringbuffer_test tests/ringbuffer_test.cpp)
target_include_directories(ringbuffer_test PRIVATE include)
target_link_libraries(ringbuffer_test GTest::gtest_main)
add_test(NAME ringbuffer_test COMMAND ringbuffer_test)

add_executable(orderbookclient_test tests/orderbookclient_test.cpp)
target_include_directories(orderbookclient_test PRIVATE include)
target_link_libraries(orderbookclient_test GTest::gtest_main)
add_test(NAME orderbookclient_test COMMAND orderbookclient_test)

add_executable(simfeeder_test tests/simfeeder_test.cpp)
target_include_directories(simfeeder_test PRIVATE include)
target_link_libraries(simfeeder_test GTest::gtest_main)
add_test(NAME simfeeder_test COMMAND simfeeder_test)
