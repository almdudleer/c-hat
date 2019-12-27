cmake_minimum_required(VERSION 3.15)
project(chat C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-pthread")

add_executable(server server.c common.h common.c)
add_executable(client client.c common.h common.c)