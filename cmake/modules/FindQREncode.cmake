# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#[=======================================================================[.rst:
FindQREncode
------------

Find the libqrencode library for QR code generation.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``QREncode::QREncode``
  The qrencode library

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``QREncode_FOUND``
  True if libqrencode was found.
``QREncode_INCLUDE_DIRS``
  Include directories for libqrencode.
``QREncode_LIBRARIES``
  Libraries to link against.
``QREncode_VERSION``
  Version of libqrencode found.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Allow user to specify location
set(QRENCODE_ROOT "$ENV{QRENCODE_ROOT}" CACHE PATH "libqrencode installation root")

# Search paths
set(_QRENCODE_SEARCH_PATHS
    ${QRENCODE_ROOT}
    /usr/local
    /usr
    /opt/local
)

# Find header
find_path(QREncode_INCLUDE_DIR
    NAMES qrencode.h
    HINTS ${_QRENCODE_SEARCH_PATHS}
    PATH_SUFFIXES include
)

# Find library
find_library(QREncode_LIBRARY
    NAMES qrencode
    HINTS ${_QRENCODE_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# Extract version
if(QREncode_INCLUDE_DIR AND EXISTS "${QREncode_INCLUDE_DIR}/qrencode.h")
    file(STRINGS "${QREncode_INCLUDE_DIR}/qrencode.h" _QRENCODE_MAJOR_STR
        REGEX "^#define[ \t]+QRENCODE_VERSION_MAJOR[ \t]+[0-9]+")
    file(STRINGS "${QREncode_INCLUDE_DIR}/qrencode.h" _QRENCODE_MINOR_STR
        REGEX "^#define[ \t]+QRENCODE_VERSION_MINOR[ \t]+[0-9]+")
    file(STRINGS "${QREncode_INCLUDE_DIR}/qrencode.h" _QRENCODE_MICRO_STR
        REGEX "^#define[ \t]+QRENCODE_VERSION_MICRO[ \t]+[0-9]+")

    if(_QRENCODE_MAJOR_STR AND _QRENCODE_MINOR_STR AND _QRENCODE_MICRO_STR)
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _QRENCODE_MAJOR "${_QRENCODE_MAJOR_STR}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _QRENCODE_MINOR "${_QRENCODE_MINOR_STR}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _QRENCODE_MICRO "${_QRENCODE_MICRO_STR}")
        set(QREncode_VERSION "${_QRENCODE_MAJOR}.${_QRENCODE_MINOR}.${_QRENCODE_MICRO}")
    endif()
endif()

find_package_handle_standard_args(QREncode
    REQUIRED_VARS
        QREncode_LIBRARY
        QREncode_INCLUDE_DIR
    VERSION_VAR QREncode_VERSION
)

if(QREncode_FOUND)
    set(QREncode_INCLUDE_DIRS ${QREncode_INCLUDE_DIR})
    set(QREncode_LIBRARIES ${QREncode_LIBRARY})

    if(NOT TARGET QREncode::QREncode)
        add_library(QREncode::QREncode UNKNOWN IMPORTED)
        set_target_properties(QREncode::QREncode PROPERTIES
            IMPORTED_LOCATION "${QREncode_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${QREncode_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(QREncode_INCLUDE_DIR QREncode_LIBRARY)
