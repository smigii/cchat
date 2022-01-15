cmake_minimum_required(VERSION 3.20)
project(cdemon C)

set(CMAKE_C_STANDARD 11)

add_executable(cdemon cdemon.c)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(cdemon PRIVATE Threads::Threads)
