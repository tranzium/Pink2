# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# CMake toolchain file for cross-compiling to Windows 64-bit using MinGW-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cross compiler prefix
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Cross compilers
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
find_program(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

if(NOT CMAKE_C_COMPILER)
    message(FATAL_ERROR "Cannot find ${TOOLCHAIN_PREFIX}-gcc")
endif()

# Target environment
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Adjust the default behavior of FIND_XXX() commands
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Windows-specific definitions
add_definitions(-DWIN32 -D_WIN32 -D__WINDOWS__)
add_definitions(-DWINVER=0x0601 -D_WIN32_WINNT=0x0601)  # Windows 7+
add_definitions(-DUNICODE -D_UNICODE)

# MinGW threading fixes
add_definitions(-D_MT)
add_definitions(-DBOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN)

# Static linking for Windows builds
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")

# Windows libraries commonly needed
set(WIN32_LIBRARIES
    ws2_32
    mswsock
    shlwapi
    ole32
    oleaut32
    uuid
    gdi32
    iphlpapi
    crypt32
)

# Boost configuration for Windows
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(Boost_THREADAPI win32)

# Qt configuration
set(QT_HOST_PATH "" CACHE PATH "Path to host Qt installation for tools")

# Allow user to specify dependency paths
set(DEPS_ROOT "/usr/${TOOLCHAIN_PREFIX}" CACHE PATH "Root path for dependencies")

# BerkeleyDB paths
set(BDB_ROOT "${DEPS_ROOT}" CACHE PATH "BerkeleyDB installation root")

# Boost paths
set(BOOST_ROOT "${DEPS_ROOT}" CACHE PATH "Boost installation root")

# OpenSSL paths
set(OPENSSL_ROOT_DIR "${DEPS_ROOT}" CACHE PATH "OpenSSL installation root")

# miniupnpc paths
set(MINIUPNPC_ROOT "${DEPS_ROOT}" CACHE PATH "miniupnpc installation root")

# qrencode paths
set(QRENCODE_ROOT "${DEPS_ROOT}" CACHE PATH "libqrencode installation root")
