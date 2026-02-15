# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#[=======================================================================[.rst:
FindLibsecp256k1
----------------

Find libsecp256k1 library (Bitcoin's optimized secp256k1 elliptic curve library).

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``Libsecp256k1::Libsecp256k1``
  The secp256k1 library

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``Libsecp256k1_FOUND``
  True if the system has libsecp256k1.
``Libsecp256k1_INCLUDE_DIRS``
  Include directories needed for libsecp256k1.
``Libsecp256k1_LIBRARIES``
  Libraries needed to link to libsecp256k1.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``Libsecp256k1_INCLUDE_DIR``
  The directory containing ``secp256k1.h``.
``Libsecp256k1_LIBRARY``
  The path to the libsecp256k1 library.

Hints
^^^^^

``SECP256K1_ROOT``
  Environment variable or CMake variable pointing to libsecp256k1 installation.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Allow user to specify location
if(NOT DEFINED SECP256K1_ROOT)
    set(SECP256K1_ROOT "$ENV{SECP256K1_ROOT}" CACHE PATH "libsecp256k1 installation root")
endif()

# If SECP256K1_ROOT is explicitly set, check there FIRST
if(SECP256K1_ROOT AND EXISTS "${SECP256K1_ROOT}/include/secp256k1.h")
    message(STATUS "Using explicit SECP256K1_ROOT: ${SECP256K1_ROOT}")
    set(Libsecp256k1_INCLUDE_DIR "${SECP256K1_ROOT}/include" CACHE PATH "libsecp256k1 include dir" FORCE)

    # Find the library in SECP256K1_ROOT
    if(EXISTS "${SECP256K1_ROOT}/lib/libsecp256k1.a")
        set(Libsecp256k1_LIBRARY "${SECP256K1_ROOT}/lib/libsecp256k1.a" CACHE FILEPATH "libsecp256k1 library" FORCE)
    else()
        find_library(Libsecp256k1_LIBRARY
            NAMES secp256k1
            HINTS "${SECP256K1_ROOT}"
            PATH_SUFFIXES lib lib64
            NO_DEFAULT_PATH
        )
    endif()
else()
    # Search standard paths
    find_path(Libsecp256k1_INCLUDE_DIR
        NAMES secp256k1.h
        PATH_SUFFIXES include
    )

    find_library(Libsecp256k1_LIBRARY
        NAMES secp256k1
        PATH_SUFFIXES lib lib64
    )
endif()

# Verify required module headers exist
if(Libsecp256k1_INCLUDE_DIR)
    foreach(_header secp256k1_recovery.h secp256k1_ecdh.h)
        if(NOT EXISTS "${Libsecp256k1_INCLUDE_DIR}/${_header}")
            message(WARNING "libsecp256k1 found but missing ${_header} — recovery and ECDH modules required")
            set(Libsecp256k1_INCLUDE_DIR "Libsecp256k1_INCLUDE_DIR-NOTFOUND" CACHE PATH "libsecp256k1 include dir" FORCE)
        endif()
    endforeach()
endif()

find_package_handle_standard_args(Libsecp256k1
    REQUIRED_VARS
        Libsecp256k1_LIBRARY
        Libsecp256k1_INCLUDE_DIR
)

if(Libsecp256k1_FOUND)
    set(Libsecp256k1_INCLUDE_DIRS ${Libsecp256k1_INCLUDE_DIR})
    set(Libsecp256k1_LIBRARIES ${Libsecp256k1_LIBRARY})

    if(NOT TARGET Libsecp256k1::Libsecp256k1)
        add_library(Libsecp256k1::Libsecp256k1 UNKNOWN IMPORTED)
        set_target_properties(Libsecp256k1::Libsecp256k1 PROPERTIES
            IMPORTED_LOCATION "${Libsecp256k1_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Libsecp256k1_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(Libsecp256k1_INCLUDE_DIR Libsecp256k1_LIBRARY)
