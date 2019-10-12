cmake_minimum_required(VERSION 3.1)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(--std=c++17)

project(BetteRConTestBench)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)

set(PROJECT_LINK_LIBS libBetteRConFramework.a)

link_directories(../BetteRConFramework/build)

include_directories(../BetteRConFramework/include)
include_directories(../../asio/asio/include)

find_package (Threads)

add_executable(BetteRConTestBench BetteRConTestBench.cpp)
target_link_libraries(BetteRConTestBench ${PROJECT_LINK_LIBS} ${CMAKE_THREAD_LIBS_INIT})