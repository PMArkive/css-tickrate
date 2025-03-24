# NOTE: I use Clang-CL on Windows, so I must use this version of CMake.
cmake_minimum_required(VERSION 3.15)

project(tickrate
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
    GIT_TAG 11.1.4
    OPTIONS "FMT_INSTALL OFF")
CPMAddPackage(NAME zydis
    GITHUB_REPOSITORY zyantific/zydis
    VERSION 4.1.1
    EXCLUDE_FROM_ALL YES
    OPTIONS "ZYDIS_FEATURE_FORMATTER OFF"
    "ZYDIS_FEATURE_ENCODER OFF"
    "ZYDIS_BUILD_TOOLS OFF"
    "ZYDIS_BUILD_EXAMPLES OFF")
CPMAddPackage(NAME scope_guard
    GITHUB_REPOSITORY ricab/scope_guard
    VERSION 1.1.0
    DOWNLOAD_ONLY ON)
CPMAddPackage(NAME lua
    GITHUB_REPOSITORY lua/lua
    VERSION 5.4.7
    DOWNLOAD_ONLY YES)
CPMAddPackage("gh:TartanLlama/expected@1.1.0")
CPMAddPackage("gh:ThePhD/sol2@3.5.0")

# Lua has no CMake support, so we create our own target.
if (lua_ADDED)
    file(GLOB lua_headers ${lua_SOURCE_DIR}/*.h)
    file(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
    list(REMOVE_ITEM lua_sources ${lua_SOURCE_DIR}/lua.c ${lua_SOURCE_DIR}/luac.c ${lua_SOURCE_DIR}/onelua.c)
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${lua_headers} ${lua_sources})
    add_library(lua STATIC ${lua_headers} ${lua_sources})
    target_include_directories(lua PUBLIC $<BUILD_INTERFACE:${lua_SOURCE_DIR}>)
endif ()

if (scope_guard_ADDED)
    add_library(scope_guard INTERFACE)
    add_library(scope_guard::scope_guard ALIAS scope_guard)
    target_include_directories(scope_guard INTERFACE ${scope_guard_SOURCE_DIR})
endif ()

set(safetyhook_headers src/safetyhook/include/safetyhook/safetyhook.hpp)
set(safetyhook_sources src/safetyhook/safetyhook.cpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${safetyhook_headers} ${safetyhook_sources})
add_library(safetyhook STATIC ${safetyhook_headers} ${safetyhook_sources})
add_library(safetyhook::safetyhook ALIAS safetyhook)
target_compile_features(safetyhook PUBLIC cxx_std_17)
target_include_directories(safetyhook PRIVATE src/safetyhook/include/safetyhook PUBLIC src/safetyhook/include)
target_link_libraries(safetyhook PUBLIC tl::expected Zydis)

set(tickrate_headers
    src/common.hpp
    src/type.hpp
    src/os.hpp
    src/utl.hpp
    src/game.hpp
    src/lua/lua_state.hpp
    src/lua/lua_loader.hpp)
set(tickrate_sources
    src/os.cpp
    src/utl.cpp
    src/game.cpp
    src/lua/lua_state.cpp
    src/lua/lua_loader.cpp
    src/main.cpp)

if (WIN32)
    list(APPEND tickrate_sources src/os.windows.cpp)
elseif (UNIX)
    list(APPEND tickrate_sources src/os.linux.cpp)
else ()
    message(FATAL_ERROR "tickrate: Unsupported OS.")
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${tickrate_headers} ${tickrate_sources})
add_library(tickrate SHARED ${tickrate_headers} ${tickrate_sources})
target_compile_features(tickrate PRIVATE cxx_std_17)
target_compile_definitions(tickrate PRIVATE NOMINMAX SOL_NO_EXCEPTIONS=1 SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=0)
target_include_directories(tickrate PRIVATE src)
target_link_libraries(tickrate
    PRIVATE
    tl::expected
    scope_guard::scope_guard
    fmt::fmt
    safetyhook::safetyhook
    sol2::sol2
    Zydis
    lua)

if (WIN32)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # Clang-CL
        # I only have this one filled out because it's my primary compiler on Windows.
        target_compile_options(tickrate
            PRIVATE
            # /clang:-g
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
        message(FATAL_ERROR "tickrate: Unsupported compiler.")
    endif ()
else ()
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # Clang
    else ()
        message(FATAL_ERROR "tickrate: Unsupported compiler.")
    endif ()
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(tr_arch_suffix "_x86-64")
else ()
    set(tr_arch_suffix "_x86-32")
endif ()

# File paths.
set(tr_bin_filename "tickrate${tr_arch_suffix}")
set(tr_vdf_filepath "${CMAKE_BINARY_DIR}/${tr_bin_filename}.vdf")
set_target_properties(tickrate PROPERTIES OUTPUT_NAME "${tr_bin_filename}")

# Install files.
install(TARGETS tickrate
    RUNTIME DESTINATION "addons"
    LIBRARY DESTINATION "addons")
file(WRITE "${tr_vdf_filepath}"
    "\"Plugin\"\n"
    "{\n"
    "    \"file\" \"addons/${tr_bin_filename}\"\n"
    "}\n")
install(FILES "${tr_vdf_filepath}" DESTINATION "addons")
install(DIRECTORY DESTINATION "addons/tickrate")
install(DIRECTORY DESTINATION "addons/tickrate/autorun")
