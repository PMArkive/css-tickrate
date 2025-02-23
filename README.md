# CS:S Tickrate (Server addon)

A _standalone_ **Counter-Strike: Source** server addon that enables the `-tickrate` command line parameter.

## Usage & Installation

Download a release zip from [here](https://github.com/angelfor3v3r/css-tickrate/releases) and extract the `addons` folder into your server's `cstrike` folder.

You can now pass `-tickrate <Desired Tickrate>` on the command line:
```
srcds.exe -console -game cstrike +maxplayers 10 +sv_maxupdaterate 100 +sv_maxcmdrate 100 +map de_nuke -tickrate 100
```

Note that you must set ConVars such as `sv_maxupdaterate`, `sv_maxcmdrate`, etc to accommodate the new tickrate setting.

## Building

If the releases don't fit your needs then you can build the library yourself.\
Building requires **CMake** and a **C++17** ready compiler.

Here are some examples (assuming a 64-bit OS):

### Windows (MSVC):

```
64-bit:
    cmake -G "Visual Studio 17 2022" -A x64 -B cmake-build-x86_64
    cmake --build cmake-build-x86_64 --config Release
    
32-bit:
    cmake -G "Visual Studio 17 2022" -A Win32 -B cmake-build-x86_32
    cmake --build cmake-build-x86_32 --config Release
```

### Linux (GCC):

```
64-bit: 
    cmake -B cmake-build-x86_64 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
    cmake --build cmake-build-x86_64 --config Release
    
32-bit: 
    cmake -B cmake-build-x86_32 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release
    cmake --build cmake-build-x86_32 --config Release
```

## Thanks
[SafetyHook](https://github.com/cursey/safetyhook)\
[Zydis](https://github.com/zyantific/zydis)\
[{fmt}](https://github.com/fmtlib/fmt)\
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)
