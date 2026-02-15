# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# CMake toolchain file for cross-compiling to Windows 64-bit using MXE

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# MXE installation root - can be overridden
if(NOT DEFINED MXE_ROOT)
    set(MXE_ROOT "/usr/lib/mxe" CACHE PATH "MXE installation root")
endif()

# MXE target (static build)
set(MXE_TARGET "x86_64-w64-mingw32.static")
set(MXE_TARGET_ROOT "${MXE_ROOT}/usr/${MXE_TARGET}")
set(MXE_BIN_DIR "${MXE_ROOT}/usr/bin")

# Cross compiler prefix
set(TOOLCHAIN_PREFIX ${MXE_TARGET})

# Cross compilers - use full path to MXE tools
set(CMAKE_C_COMPILER "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-g++")
set(CMAKE_RC_COMPILER "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-windres")
set(CMAKE_AR "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-ar")
set(CMAKE_RANLIB "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-ranlib")
set(CMAKE_STRIP "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-strip")

# Verify compiler exists
if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR "MXE compiler not found: ${CMAKE_C_COMPILER}\nMake sure MXE is installed at ${MXE_ROOT}")
endif()

# Target environment - MXE root paths
set(CMAKE_FIND_ROOT_PATH "${MXE_TARGET_ROOT}")
set(CMAKE_PREFIX_PATH "${MXE_TARGET_ROOT}")

# pkg-config for MXE
set(ENV{PKG_CONFIG_PATH} "${MXE_TARGET_ROOT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${MXE_TARGET_ROOT}/lib/pkgconfig")
set(PKG_CONFIG_EXECUTABLE "${MXE_BIN_DIR}/${TOOLCHAIN_PREFIX}-pkg-config")

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

# Static linking for Windows builds (MXE is static by default)
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

# Boost configuration for MXE
# MXE boost library naming: libboost_<component>-mt-x64.a
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME OFF)  # MXE doesn't use -s suffix for static runtime
set(Boost_THREADAPI win32)
set(Boost_NO_SYSTEM_PATHS ON)
set(BOOST_ROOT "${MXE_TARGET_ROOT}")
set(BOOST_INCLUDEDIR "${MXE_TARGET_ROOT}/include")
set(BOOST_LIBRARYDIR "${MXE_TARGET_ROOT}/lib")

# MXE boost library naming: libboost_<component>-mt-x64.a
set(Boost_COMPILER "")
set(Boost_ARCHITECTURE "-x64")

# BerkeleyDB 4.8 paths (required for wallet.dat compatibility)
# Use custom-built BDB 4.8, NOT MXE's BDB 6.x
set(BDB_ROOT "/opt/mxe-bdb48" CACHE PATH "BerkeleyDB 4.8 installation root")

# libsecp256k1 paths (cross-compiled from bitcoin-core/secp256k1 v0.2.0)
set(SECP256K1_ROOT "/opt/mxe-secp256k1" CACHE PATH "libsecp256k1 installation root")

# OpenSSL paths
set(OPENSSL_ROOT_DIR "${MXE_TARGET_ROOT}" CACHE PATH "OpenSSL installation root")
set(OPENSSL_USE_STATIC_LIBS TRUE)

# zlib paths
set(ZLIB_ROOT "${MXE_TARGET_ROOT}" CACHE PATH "zlib installation root")

# miniupnpc paths (if available)
set(MINIUPNPC_ROOT "${MXE_TARGET_ROOT}" CACHE PATH "miniupnpc installation root")

# qrencode paths (if available)
set(QRENCODE_ROOT "${MXE_TARGET_ROOT}" CACHE PATH "libqrencode installation root")

# Qt configuration (if building GUI)
# Qt5 is in MXE's qt5 subdirectory
set(Qt5_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5" CACHE PATH "Qt5 CMake directory")
set(Qt5Core_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5Core" CACHE PATH "Qt5Core CMake directory")
set(Qt5Widgets_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5Widgets" CACHE PATH "Qt5Widgets CMake directory")
set(Qt5Gui_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5Gui" CACHE PATH "Qt5Gui CMake directory")
set(Qt5Network_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5Network" CACHE PATH "Qt5Network CMake directory")
set(Qt5Svg_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5Svg" CACHE PATH "Qt5Svg CMake directory")
set(Qt5LinguistTools_DIR "${MXE_TARGET_ROOT}/qt5/lib/cmake/Qt5LinguistTools" CACHE PATH "Qt5LinguistTools CMake directory")

# Qt host tools (moc, rcc, uic) for cross-compilation - use MXE's native wrappers
set(QT_HOST_PATH "${MXE_TARGET_ROOT}/qt5" CACHE PATH "Path to host Qt installation for tools")
