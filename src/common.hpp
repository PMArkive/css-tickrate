#pragma once

#if defined(_MSC_VER)
#define TR_COMPILER_MSVC  1
#define TR_COMPILER_CLANG 0
#define TR_COMPILER_GCC   0
#elif defined(__clang__)
#define TR_COMPILER_MSVC  0
#define TR_COMPILER_CLANG 1
#define TR_COMPILER_GCC   0
#elif defined(__GNUC__)
#define TR_COMPILER_MSVC  0
#define TR_COMPILER_CLANG 0
#define TR_COMPILER_GCC   1
#else
#error "CS:S Tickrate: Unsupported compiler"
#endif

#if TR_COMPILER_MSVC
#if defined(_M_X64)
#define TR_ARCH_X86_64 1
#define TR_ARCH_X86_32 0
#elif defined(_M_IX86)
#define TR_ARCH_X86_64 0
#define TR_ARCH_X86_32 1
#else
#error "CS:S Tickrate: Unsupported architecture"
#endif
#elif TR_COMPILER_CLANG || TR_COMPILER_GCC
#if defined(__x86_64__)
#define TR_ARCH_X86_64 1
#define TR_ARCH_X86_32 0
#elif defined(__i386__)
#define TR_ARCH_X86_64 0
#define TR_ARCH_X86_32 1
#else
#error "CS:S Tickrate: Unsupported architecture"
#endif
#endif

#if defined(_WIN32)
#define TR_OS_WINDOWS 1
#define TR_OS_LINUX   0
#elif defined(__linux__)
#define TR_OS_WINDOWS 0
#define TR_OS_LINUX   1
#else
#error "CS:S Tickrate: Unsupported OS."
#endif

#if TR_OS_WINDOWS
#if TR_COMPILER_MSVC
#define TR_CCALL    __cdecl
#define TR_STDCALL  __stdcall
#define TR_FASTCALL __fastcall
#define TR_THISCALL __thiscall
#elif TR_COMPILER_CLANG || TR_COMPILER_GCC
#define TR_CCALL    __attribute__((cdecl))
#define TR_STDCALL  __attribute__((stdcall))
#define TR_FASTCALL __attribute__((fastcall))
#define TR_THISCALL __attribute__((thiscall))
#endif
#else
#define TR_CCALL
#define TR_STDCALL
#define TR_FASTCALL
#define TR_THISCALL
#endif

#if TR_COMPILER_MSVC
#define TR_DLLIMPORT __declspec(dllimport)
#define TR_DLLEXPORT __declspec(dllexport)
#elif TR_COMPILER_CLANG || TR_COMPILER_GCC
#define TR_DLLIMPORT
#define TR_DLLEXPORT __attribute__((visibility("default")))
#endif
