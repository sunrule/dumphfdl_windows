# toolchain-mingw32.cmake - CMake toolchain для 32-битной сборки (MSYS2 MinGW32)
#
# Используется для сборки dumphfdl.dll и dumphfdl.exe под 32-бит Windows
# (в частности, для Borland Delphi 7).
#
# Зависимости (fftw3f, libacars, liquid-dsp, libconfig) должны быть заранее
# собраны из исходников в префикс (см. scripts/build-all.sh).
#
# Использование:
#   cmake -B build-i686 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
#
# Путь к префиксу можно переопределить:
#   cmake -B build-i686 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake \
#         -DI686_PREFIX=/path/to/prefix

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

# MSYS2 MinGW32 (32-бит).
# Note: CMake требует нативных (Windows) путей - MSYS /c/... пути
# не понимаются сборочными инструментами Windows.
set(MSYS32 "C:/msys64/mingw32")

set(CMAKE_C_COMPILER "${MSYS32}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${MSYS32}/bin/g++.exe")
set(CMAKE_RC_COMPILER "${MSYS32}/bin/windres.exe")
set(CMAKE_AR "${MSYS32}/bin/ar.exe")
set(CMAKE_RANLIB "${MSYS32}/bin/ranlib.exe")
set(CMAKE_STRIP "${MSYS32}/bin/strip.exe")

# Префикс с i686-зависимостями (по умолчанию - build/prefix-x86 от
# scripts/build-all.sh; переопределяется через -DI686_PREFIX=...).
if(NOT DEFINED I686_PREFIX)
    set(I686_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../build/prefix-x86")
endif()
# Разворачиваем относительный путь в абсолютный
get_filename_component(I686_PREFIX "${I686_PREFIX}" ABSOLUTE)

set(CMAKE_FIND_ROOT_PATH "${I686_PREFIX}" "${MSYS32}")
set(CMAKE_PREFIX_PATH "${I686_PREFIX}" "${MSYS32}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
