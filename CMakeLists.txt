cmake_minimum_required(VERSION 3.20)
project(cchat C)

set(CMAKE_C_STANDARD 11)

add_executable(cchat-send cchat_send.c)
add_executable(cchat_recv cchat_recv.c)
add_executable(cdemon cdemon.c)
