cmake_minimum_required(VERSION 3.20)
project(craven C)

set(CMAKE_C_STANDARD 11)

add_executable(craven craven.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(craven PRIVATE Threads::Threads)
