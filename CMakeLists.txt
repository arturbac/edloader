# edloader: the d3d11.dll that chains proxies from edloader.txt (README).
# Built on Linux with clang-cl against MSVC's and the Windows SDK's headers and libraries
# (edplugin_common/cmake/clang-cl-msvc-sdk.cmake, MSVC_ROOT).
cmake_minimum_required(VERSION 3.29)

# the shared library edplugin_common, a clone in this directory on the commit edplugin_common.commit pins: the
# toolchain is in it, so it is looked for before project()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/edplugin_common/CMakeLists.txt")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/edplugin_common.commit" edplugin_common_pinned LIMIT_COUNT 1)
  string(STRIP "${edplugin_common_pinned}" edplugin_common_pinned)
  message(FATAL_ERROR "no clone of edplugin_common in ${CMAKE_CURRENT_SOURCE_DIR}; in that directory:\n"
                      "  git clone https://github.com/arturbac/edplugin_common.git edplugin_common\n"
                      "  git -C edplugin_common checkout ${edplugin_common_pinned}")
endif()

project(edloader LANGUAGES CXX ASM_MASM)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  message(FATAL_ERROR "edloader builds with clang-cl: configure with a preset or -DCMAKE_TOOLCHAIN_FILE=edplugin_common/cmake/clang-cl-msvc-sdk.cmake")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

# edplugin_common: the code and the build steps the plugins share, with CPM and stralgo (string operations: merge in
# place of chained +; small_vectors under it), pinned there. CPM_SOURCE_CACHE (a filled cache) and
# FETCHCONTENT_FULLY_DISCONNECTED=ON configure without the network
add_subdirectory(edplugin_common)

# exports: the Windows d3d11 names from a list, a thunk each but the two edloader defines (gen/d3d11_proxy.*)
edplugin_common_proxy_exports(d3d11 tools/d3d11_exports.txt WRAP D3D11CreateDevice D3D11CreateDeviceAndSwapChain)

add_library(edloader SHARED src/proxy.cc src/chain.cc src/takeovers.cc src/pe_read.cc src/log_paths.cc src/window.cc
                     "${d3d11_PROXY_ASM}" "${d3d11_PROXY_DEF}")
target_compile_definitions(edloader PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(edloader PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
target_link_libraries(edloader PRIVATE kernel32 user32 edplugin_common::edplugin_common)
target_link_options(edloader PRIVATE /DEBUG:FULL $<$<NOT:$<CONFIG:Debug>>:/OPT:REF /OPT:ICF>)
set_target_properties(edloader PROPERTIES
  PREFIX ""
  OUTPUT_NAME d3d11
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
edplugin_common_version(EDLOADER_VERSION edloader_version.h edloader)
edplugin_common_pin(edloader)

option(EDLOADER_TESTS "build the chain test's dlls (tools/test.sh runs them under wine)" ON)
if(EDLOADER_TESTS)
  add_subdirectory(test)
endif()
