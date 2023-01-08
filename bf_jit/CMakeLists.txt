cmake_minimum_required(VERSION 3.0.0)
project(bf_jit VERSION 0.1.0)

set(ASMJIT_DIR ${PROJECT_SOURCE_DIR}/ext/asmjit)
# print asmjit dir
message(STATUS "ASMJIT_DIR: ${ASMJIT_DIR}")
set(ASMJIT_STATIC ON) 
add_definitions(-DASMJIT_STATIC) 
include_directories(${ASMJIT_DIR}/src/) 
add_subdirectory(${ASMJIT_DIR}) 

add_executable(bf_jit main.cpp)

target_link_libraries(bf_jit asmjit)
