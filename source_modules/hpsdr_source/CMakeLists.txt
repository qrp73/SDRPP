cmake_minimum_required(VERSION 3.13)
project(hpsdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(hpsdr_source PRIVATE "src/")
