# edloader: the d3d11.dll that chains proxies from edloader.txt (README).
# Built on Linux with clang-cl against MSVC's and the Windows SDK's headers and libraries
# (cmake/clang-cl-msvc-sdk.cmake, MSVC_ROOT).
cmake_minimum_required(VERSION 3.29)
project(edloader LANGUAGES CXX ASM_MASM)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  message(FATAL_ERROR "edloader builds with clang-cl: configure with a preset or -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-msvc-sdk.cmake")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(gen "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${gen}")

# exports: the Windows d3d11 names from a list, thunked by EDVR's generator (tools/gen_exports.py, MIT)
add_custom_command(
  OUTPUT "${gen}/edvr_d3d11.def" "${gen}/edvr_thunks_d3d11.asm" "${gen}/edvr_exports_d3d11.inc"
  COMMAND "${Python3_EXECUTABLE}" tools/gen_exports.py --self-test
  COMMAND "${Python3_EXECUTABLE}" tools/gen_exports_from_list.py --source tools/d3d11_exports.txt --tag d3d11
          --out "${gen}" --wrap D3D11CreateDevice --wrap D3D11CreateDeviceAndSwapChain
  DEPENDS tools/gen_exports.py tools/gen_exports_from_list.py tools/d3d11_exports.txt
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "d3d11 exports"
  VERBATIM)
add_custom_target(edloader_generated DEPENDS
  "${gen}/edvr_d3d11.def" "${gen}/edvr_thunks_d3d11.asm" "${gen}/edvr_exports_d3d11.inc")

add_custom_target(edloader_version
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${gen}/edloader_version.h -DNAME=EDLOADER_VERSION
          -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
  BYPRODUCTS "${gen}/edloader_version.h"
  COMMENT "edloader version"
  VERBATIM)

set_source_files_properties("${gen}/edvr_thunks_d3d11.asm" PROPERTIES GENERATED TRUE)
add_library(edloader SHARED src/edloader.cc "${gen}/edvr_thunks_d3d11.asm" "${gen}/edvr_d3d11.def")
target_compile_definitions(edloader PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
target_include_directories(edloader PRIVATE "${gen}")
target_compile_options(edloader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /FI${gen}/edloader_version.h>)
target_link_libraries(edloader PRIVATE kernel32)
target_link_options(edloader PRIVATE /DEBUG:FULL $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>)
set_target_properties(edloader PROPERTIES
  PREFIX ""
  OUTPUT_NAME d3d11
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(edloader edloader_version edloader_generated)

option(EDLOADER_TESTS "build the chain test's dlls (tools/test.sh runs them under wine)" ON)
if(EDLOADER_TESTS)
  add_subdirectory(test)
endif()
