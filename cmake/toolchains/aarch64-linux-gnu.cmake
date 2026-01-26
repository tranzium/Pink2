# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# CMake toolchain file for cross-compiling to ARM64 (Raspberry Pi 3/4/5, ARM servers)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler prefix
set(TOOLCHAIN_PREFIX aarch64-linux-gnu)

# Cross compilers
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
find_program(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)

if(NOT CMAKE_C_COMPILER)
    message(FATAL_ERROR "Cannot find ${TOOLCHAIN_PREFIX}-gcc")
endif()

# Target environment
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Sysroot if available
if(EXISTS "/usr/${TOOLCHAIN_PREFIX}/sysroot")
    set(CMAKE_SYSROOT /usr/${TOOLCHAIN_PREFIX}/sysroot)
endif()

# Adjust the default behavior of FIND_XXX() commands
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ARM64-specific compiler flags
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")

# Linux definitions
add_definitions(-DLINUX)

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
