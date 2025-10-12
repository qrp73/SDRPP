cmake_minimum_required(VERSION 3.13)
project(kiwisdr_source)

find_package(OpenSSL REQUIRED)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(kiwisdr_source PRIVATE "src/")

target_link_libraries(kiwisdr_source PRIVATE OpenSSL::Crypto)
