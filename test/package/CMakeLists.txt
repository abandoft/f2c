cmake_minimum_required(VERSION 3.20)
project(f2c_package_check LANGUAGES C)

if(NOT IS_DIRECTORY "${PACKAGED_INCLUDE_DIR}")
    message(FATAL_ERROR "PACKAGED_INCLUDE_DIR must point to the staged public headers")
endif()

add_library(f2c_package_header OBJECT header.c)
target_include_directories(f2c_package_header PRIVATE "${PACKAGED_INCLUDE_DIR}")
set_target_properties(f2c_package_header PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
