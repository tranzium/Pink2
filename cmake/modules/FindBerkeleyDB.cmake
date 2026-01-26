# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#[=======================================================================[.rst:
FindBerkeleyDB
--------------

Find Berkeley DB library (specifically version 4.8 for cryptocurrency wallet compatibility).

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``BerkeleyDB::BerkeleyDB``
  The Berkeley DB library

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``BerkeleyDB_FOUND``
  True if the system has Berkeley DB.
``BerkeleyDB_INCLUDE_DIRS``
  Include directories needed for Berkeley DB.
``BerkeleyDB_LIBRARIES``
  Libraries needed to link to Berkeley DB.
``BerkeleyDB_VERSION``
  Version of Berkeley DB found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``BerkeleyDB_INCLUDE_DIR``
  The directory containing ``db_cxx.h``.
``BerkeleyDB_LIBRARY``
  The path to the Berkeley DB library.

Hints
^^^^^

``BDB_ROOT``
  Environment variable or CMake variable pointing to Berkeley DB installation.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Allow user to specify BDB location
if(NOT DEFINED BDB_ROOT)
    set(BDB_ROOT "$ENV{BDB_ROOT}" CACHE PATH "Berkeley DB installation root")
endif()

# If BDB_ROOT is explicitly set, check there FIRST (important for cross-compilation
# where we want BDB 4.8, not the toolchain's newer version)
if(BDB_ROOT AND EXISTS "${BDB_ROOT}/include/db_cxx.h")
    message(STATUS "Using explicit BDB_ROOT: ${BDB_ROOT}")
    set(BerkeleyDB_INCLUDE_DIR "${BDB_ROOT}/include" CACHE PATH "BerkeleyDB include dir" FORCE)

    # Find the library in BDB_ROOT
    if(EXISTS "${BDB_ROOT}/lib/libdb_cxx.a")
        set(BerkeleyDB_LIBRARY "${BDB_ROOT}/lib/libdb_cxx.a" CACHE FILEPATH "BerkeleyDB library" FORCE)
    elseif(EXISTS "${BDB_ROOT}/lib/libdb_cxx-4.8.a")
        set(BerkeleyDB_LIBRARY "${BDB_ROOT}/lib/libdb_cxx-4.8.a" CACHE FILEPATH "BerkeleyDB library" FORCE)
    endif()
else()
    # Common search paths (fallback)
    set(_BDB_SEARCH_PATHS
        ${BDB_ROOT}
        /usr/local/opt/berkeley-db@4
        /usr/local/opt/berkeley-db4
        /usr/local/BerkeleyDB.4.8
        /opt/local
        /usr/local
        /usr
    )

    # Version-specific library suffixes to try
    set(_BDB_LIB_SUFFIXES
        db_cxx-4.8
        db_cxx-4
        db4_cxx
        db_cxx
    )

    # Search for header
    find_path(BerkeleyDB_INCLUDE_DIR
        NAMES db_cxx.h
        HINTS ${_BDB_SEARCH_PATHS}
        PATH_SUFFIXES
            include
            include/db4
            include/db48
            include/db4.8
            include/berkeley-db
            include/berkeley-db-4.8
    )

    # Search for library
    foreach(_LIB_NAME ${_BDB_LIB_SUFFIXES})
        find_library(BerkeleyDB_LIBRARY
            NAMES ${_LIB_NAME}
            HINTS ${_BDB_SEARCH_PATHS}
            PATH_SUFFIXES
                lib
                lib64
                lib/db4
                lib/db48
                lib/db4.8
        )
        if(BerkeleyDB_LIBRARY)
            break()
        endif()
    endforeach()
endif()

# Extract version from db.h
if(BerkeleyDB_INCLUDE_DIR)
    if(EXISTS "${BerkeleyDB_INCLUDE_DIR}/db.h")
        file(STRINGS "${BerkeleyDB_INCLUDE_DIR}/db.h" _BDB_VERSION_MAJOR
            REGEX "^#define[ \t]+DB_VERSION_MAJOR[ \t]+[0-9]+")
        file(STRINGS "${BerkeleyDB_INCLUDE_DIR}/db.h" _BDB_VERSION_MINOR
            REGEX "^#define[ \t]+DB_VERSION_MINOR[ \t]+[0-9]+")
        file(STRINGS "${BerkeleyDB_INCLUDE_DIR}/db.h" _BDB_VERSION_PATCH
            REGEX "^#define[ \t]+DB_VERSION_PATCH[ \t]+[0-9]+")

        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _BDB_MAJOR "${_BDB_VERSION_MAJOR}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _BDB_MINOR "${_BDB_VERSION_MINOR}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _BDB_PATCH "${_BDB_VERSION_PATCH}")

        set(BerkeleyDB_VERSION "${_BDB_MAJOR}.${_BDB_MINOR}.${_BDB_PATCH}")
    endif()
endif()

find_package_handle_standard_args(BerkeleyDB
    REQUIRED_VARS
        BerkeleyDB_LIBRARY
        BerkeleyDB_INCLUDE_DIR
    VERSION_VAR BerkeleyDB_VERSION
)

if(BerkeleyDB_FOUND)
    set(BerkeleyDB_INCLUDE_DIRS ${BerkeleyDB_INCLUDE_DIR})
    set(BerkeleyDB_LIBRARIES ${BerkeleyDB_LIBRARY})

    if(NOT TARGET BerkeleyDB::BerkeleyDB)
        add_library(BerkeleyDB::BerkeleyDB UNKNOWN IMPORTED)
        set_target_properties(BerkeleyDB::BerkeleyDB PROPERTIES
            IMPORTED_LOCATION "${BerkeleyDB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${BerkeleyDB_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(BerkeleyDB_INCLUDE_DIR BerkeleyDB_LIBRARY)
