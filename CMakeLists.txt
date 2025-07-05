cmake_minimum_required(VERSION 3.15)
project(WheeProject LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable warnings (optional but useful)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif (MSVC)
    add_compile_options(/W4)
endif()

# Enable multithreading support
find_package(Threads REQUIRED)

# Define source files
add_executable(wheeup src/main.cpp)
target_link_libraries(wheeup PRIVATE Threads::Threads)
