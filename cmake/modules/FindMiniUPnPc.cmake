# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#[=======================================================================[.rst:
FindMiniUPnPc
-------------

Find the miniupnpc library for UPnP support.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``MiniUPnPc::MiniUPnPc``
  The miniupnpc library

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``MiniUPnPc_FOUND``
  True if miniupnpc was found.
``MiniUPnPc_INCLUDE_DIRS``
  Include directories for miniupnpc.
``MiniUPnPc_LIBRARIES``
  Libraries to link against.
``MiniUPnPc_VERSION``
  Version of miniupnpc found.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Allow user to specify location
set(MINIUPNPC_ROOT "$ENV{MINIUPNPC_ROOT}" CACHE PATH "miniupnpc installation root")

# Search paths
set(_MINIUPNPC_SEARCH_PATHS
    ${MINIUPNPC_ROOT}
    /usr/local
    /usr
    /opt/local
)

# Find header
find_path(MiniUPnPc_INCLUDE_DIR
    NAMES miniupnpc/miniupnpc.h miniupnpc.h
    HINTS ${_MINIUPNPC_SEARCH_PATHS}
    PATH_SUFFIXES include
)

# Find library
find_library(MiniUPnPc_LIBRARY
    NAMES miniupnpc
    HINTS ${_MINIUPNPC_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# Extract version
if(MiniUPnPc_INCLUDE_DIR)
    # Try to find version in miniupnpc.h
    set(_MINIUPNPC_H "${MiniUPnPc_INCLUDE_DIR}/miniupnpc/miniupnpc.h")
    if(NOT EXISTS "${_MINIUPNPC_H}")
        set(_MINIUPNPC_H "${MiniUPnPc_INCLUDE_DIR}/miniupnpc.h")
    endif()

    if(EXISTS "${_MINIUPNPC_H}")
        file(STRINGS "${_MINIUPNPC_H}" _MINIUPNPC_VERSION_STR
            REGEX "^#define[ \t]+MINIUPNPC_VERSION[ \t]+\"[^\"]+\"")
        if(_MINIUPNPC_VERSION_STR)
            string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" MiniUPnPc_VERSION "${_MINIUPNPC_VERSION_STR}")
        endif()

        # Also check for API version
        file(STRINGS "${_MINIUPNPC_H}" _MINIUPNPC_API_STR
            REGEX "^#define[ \t]+MINIUPNPC_API_VERSION[ \t]+[0-9]+")
        if(_MINIUPNPC_API_STR)
            string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" MiniUPnPc_API_VERSION "${_MINIUPNPC_API_STR}")
        endif()
    endif()
endif()

find_package_handle_standard_args(MiniUPnPc
    REQUIRED_VARS
        MiniUPnPc_LIBRARY
        MiniUPnPc_INCLUDE_DIR
    VERSION_VAR MiniUPnPc_VERSION
)

if(MiniUPnPc_FOUND)
    set(MiniUPnPc_INCLUDE_DIRS ${MiniUPnPc_INCLUDE_DIR})
    set(MiniUPnPc_LIBRARIES ${MiniUPnPc_LIBRARY})

    # Windows requires additional libraries
    if(WIN32)
        list(APPEND MiniUPnPc_LIBRARIES iphlpapi ws2_32)
    endif()

    if(NOT TARGET MiniUPnPc::MiniUPnPc)
        add_library(MiniUPnPc::MiniUPnPc UNKNOWN IMPORTED)
        set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
            IMPORTED_LOCATION "${MiniUPnPc_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MiniUPnPc_INCLUDE_DIR}"
        )

        # Add Windows dependencies
        if(WIN32)
            set_property(TARGET MiniUPnPc::MiniUPnPc APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES iphlpapi ws2_32)
        endif()
    endif()
endif()

mark_as_advanced(MiniUPnPc_INCLUDE_DIR MiniUPnPc_LIBRARY)
