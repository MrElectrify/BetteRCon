cmake_minimum_required(VERSION 2.8.9)
project(BetteRConFramework)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)

include_directories(include)
include_directories(../../asio/asio/include)

file(GLOB SOURCES "src/*.cpp" "src/Internal/*.cpp")

add_library(BetteRConFramework STATIC ${SOURCES})