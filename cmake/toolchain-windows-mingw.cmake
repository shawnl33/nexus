# Linux → Windows MinGW-w64 크로스 컴파일 툴체인
# 사용: cmake -S . -B build-windows-cross -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-mingw.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 10.0)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
# 기본은 $HOME/mingw-prefix. MINGW_PREFIX 가 있으면 그 경로를 쓴다.
if(DEFINED ENV{MINGW_PREFIX} AND NOT "$ENV{MINGW_PREFIX}" STREQUAL "")
    set(_tr_mingw_prefix "$ENV{MINGW_PREFIX}")
else()
    set(_tr_mingw_prefix "$ENV{HOME}/mingw-prefix")
endif()
if(EXISTS "${_tr_mingw_prefix}")
    list(APPEND CMAKE_FIND_ROOT_PATH "${_tr_mingw_prefix}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
