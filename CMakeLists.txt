cmake_minimum_required(VERSION 3.15)

project(css_tickrate
    VERSION 1.0.0
    LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

if (UNIX)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif ()

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# External dependencies.
include(cmake/CPM.cmake)

CPMAddPackage(NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 11.1.3
    OPTIONS "FMT_INSTALL OFF")
CPMAddPackage(NAME zydis
    GITHUB_REPOSITORY zyantific/zydis
    VERSION 4.1.1
    EXCLUDE_FROM_ALL YES
    OPTIONS "ZYDIS_FEATURE_FORMATTER OFF"
    "ZYDIS_FEATURE_ENCODER OFF"
    "ZYDIS_BUILD_TOOLS OFF"
    "ZYDIS_BUILD_EXAMPLES OFF")
CPMAddPackage("gh:TartanLlama/expected@1.1.0")

set(css_tickrate_headers
    src/common.hpp
    src/type.hpp
    src/os.hpp
    src/safetyhook/safetyhook.hpp)
set(css_tickrate_sources
    src/safetyhook/safetyhook.cpp
    src/main.cpp)

if (WIN32)
    list(APPEND css_tickrate_sources src/os.windows.cpp)
elseif (UNIX)
    list(APPEND css_tickrate_sources src/os.linux.cpp)
else ()
    message(FATAL_ERROR "css_tickrate: Unsupported OS.")
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${css_tickrate_headers} ${css_tickrate_sources})
add_library(css_tickrate SHARED ${css_tickrate_headers} ${css_tickrate_sources})
target_compile_features(css_tickrate PRIVATE cxx_std_17)
target_link_libraries(css_tickrate PRIVATE Zydis tl::expected fmt::fmt)

if (WIN32)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Clang-CL
        target_compile_options(css_tickrate
            PRIVATE
            /clang:-Wall
            /clang:-Wextra
            /clang:-Wshadow
            /clang:-pedantic
            /clang:-Werror=unused-variable
            /clang:-Wno-non-virtual-dtor
            /clang:-Wno-unused-parameter
            /clang:-Wno-unused-private-field)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # MSVC
    else ()
        message(FATAL_ERROR "css_tickrate: Unsupported compiler.")
    endif ()
else ()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Clang
    else ()
        message(FATAL_ERROR "css_tickrate: Unsupported compiler.")
    endif ()
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(tr_arch_suffix "_x86-64")
else ()
    set(tr_arch_suffix "_x86-32")
endif ()

# File paths.
set(tr_bin_filename "css_tickrate${tr_arch_suffix}")
set(tr_vdf_filepath "${CMAKE_BINARY_DIR}/${tr_bin_filename}.vdf")
set_target_properties(css_tickrate PROPERTIES OUTPUT_NAME "${tr_bin_filename}")

# Install files.
install(TARGETS css_tickrate
    RUNTIME DESTINATION "addons"
    LIBRARY DESTINATION "addons")
file(WRITE "${tr_vdf_filepath}"
    "\"Plugin\"\n"
    "{\n"
    "    \"file\" \"addons/${tr_bin_filename}\"\n"
    "}\n")
install(FILES "${tr_vdf_filepath}" DESTINATION "addons")
