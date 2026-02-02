# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.16)

# Extract version from clientversion.h
include(cmake/PinkcoinVersion.cmake)

project(Pinkcoin
    VERSION ${PINKCOIN_VERSION_MAJOR}.${PINKCOIN_VERSION_MINOR}.${PINKCOIN_VERSION_REVISION}.${PINKCOIN_VERSION_BUILD}
    LANGUAGES C CXX ASM
    DESCRIPTION "Pinkcoin cryptocurrency"
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Add custom cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

#-----------------------------------------
# Build Options
#-----------------------------------------
option(BUILD_GUI "Build Pinkcoin-Qt GUI wallet" ON)
option(BUILD_DAEMON "Build pink2d daemon" ON)
option(BUILD_TESTS "Build unit tests" ON)
option(USE_UPNP "Enable UPnP support" ON)
option(USE_QRCODE "Enable QR code support" ON)
option(USE_DBUS "Enable D-Bus notifications (Linux only)" ON)
option(ENABLE_HARDENING "Enable security hardening compiler flags" ON)
option(STATIC_BUILD "Build with static linking" OFF)

#-----------------------------------------
# Platform Detection
#-----------------------------------------
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    add_definitions(-DWIN32)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    add_definitions(-DMAC_OSX)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
    add_definitions(-DLINUX)
endif()

# Architecture detection
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_64BIT TRUE)
    add_definitions(-DIS_ARCH_64)
else()
    set(ARCH_32BIT TRUE)
endif()

#-----------------------------------------
# Compiler Flags
#-----------------------------------------
include(cmake/PinkcoinCompilerFlags.cmake)

#-----------------------------------------
# Dependencies
#-----------------------------------------

# Threading
find_package(Threads REQUIRED)

# Boost
set(Boost_USE_STATIC_LIBS ${STATIC_BUILD})
find_package(Boost 1.55 REQUIRED COMPONENTS
    filesystem
    program_options
    thread
    chrono
    unit_test_framework
    OPTIONAL_COMPONENTS
    system
)

# OpenSSL
if(STATIC_BUILD)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
find_package(OpenSSL REQUIRED)

# zlib
find_package(ZLIB REQUIRED)

# BerkeleyDB 4.8
find_package(BerkeleyDB 4.8 REQUIRED)

# Optional: miniupnpc
if(USE_UPNP)
    find_package(MiniUPnPc)
    if(MiniUPnPc_FOUND)
        add_definitions(-DUSE_UPNP=1)
        add_definitions(-DSTATICLIB)
    else()
        message(WARNING "miniupnpc not found, disabling UPnP support")
        set(USE_UPNP OFF)
    endif()
endif()

# Optional: libqrencode
if(USE_QRCODE)
    find_package(QREncode)
    if(QREncode_FOUND)
        add_definitions(-DUSE_QRCODE)
    else()
        message(WARNING "libqrencode not found, disabling QR code support")
        set(USE_QRCODE OFF)
    endif()
endif()

# Qt5 for GUI
if(BUILD_GUI)
    find_package(Qt5 5.5 REQUIRED COMPONENTS
        Core
        Widgets
        Gui
        Network
        Svg
    )

    # Optional D-Bus on Linux
    if(USE_DBUS AND PLATFORM_LINUX)
        find_package(Qt5 COMPONENTS DBus)
        if(Qt5DBus_FOUND)
            add_definitions(-DUSE_DBUS)
        else()
            message(WARNING "Qt5 DBus not found, disabling D-Bus notifications")
            set(USE_DBUS OFF)
        endif()
    endif()

    # Enable Qt auto-processing
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

#-----------------------------------------
# Build Information
#-----------------------------------------
include(cmake/GenerateBuildInfo.cmake)

#-----------------------------------------
# Common definitions
#-----------------------------------------
add_definitions(
    -DBOOST_SPIRIT_THREADSAFE
    -DBOOST_THREAD_USE_LIB
    -DHAVE_BUILD_INFO
)

# Disable Boost deprecated headers warning
add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)

#-----------------------------------------
# Subdirectories
#-----------------------------------------
add_subdirectory(src/leveldb)
add_subdirectory(src/json)
add_subdirectory(src)

if(BUILD_GUI)
    add_subdirectory(src/qt)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/test)
endif()

#-----------------------------------------
# Installation
#-----------------------------------------
include(GNUInstallDirs)

if(BUILD_DAEMON)
    install(TARGETS pink2d
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(BUILD_GUI)
    install(TARGETS Pinkcoin-Qt
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )

    # Install themes
    install(DIRECTORY src/qt/res/themes/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pinkcoin/themes
    )
endif()

#-----------------------------------------
# Summary
#-----------------------------------------
message(STATUS "")
message(STATUS "=== Pinkcoin Build Configuration ===")
message(STATUS "Version:           ${PROJECT_VERSION}")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Build targets:")
message(STATUS "  GUI (Pinkcoin-Qt): ${BUILD_GUI}")
message(STATUS "  Daemon (pink2d):   ${BUILD_DAEMON}")
message(STATUS "  Tests:             ${BUILD_TESTS}")
message(STATUS "")
message(STATUS "Optional features:")
message(STATUS "  UPnP:              ${USE_UPNP}")
message(STATUS "  QR Code:           ${USE_QRCODE}")
message(STATUS "  D-Bus:             ${USE_DBUS}")
message(STATUS "")
message(STATUS "Security:")
message(STATUS "  Hardening:         ${ENABLE_HARDENING}")
message(STATUS "  Static build:      ${STATIC_BUILD}")
message(STATUS "")
