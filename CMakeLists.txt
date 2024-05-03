cmake_minimum_required(VERSION 3.27)
project(MyThreadPool)

set(CMAKE_CXX_STANDARD 17)

add_executable(MyThreadPool main.cpp
        threadpool.h
        threadpool.cpp)
