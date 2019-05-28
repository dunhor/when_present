cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)

project(when_present)
add_executable(when_present)

target_sources(when_present PUBLIC
    main.cpp)
