# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Extract version information from src/clientversion.h

set(CLIENTVERSION_FILE "${CMAKE_SOURCE_DIR}/src/clientversion.h")

if(NOT EXISTS "${CLIENTVERSION_FILE}")
    message(FATAL_ERROR "Cannot find clientversion.h at ${CLIENTVERSION_FILE}")
endif()

file(READ "${CLIENTVERSION_FILE}" CLIENTVERSION_CONTENT)

# Extract version components using regex
string(REGEX MATCH "#define CLIENT_VERSION_MAJOR[ \t]+([0-9]+)" _ "${CLIENTVERSION_CONTENT}")
set(PINKCOIN_VERSION_MAJOR "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define CLIENT_VERSION_MINOR[ \t]+([0-9]+)" _ "${CLIENTVERSION_CONTENT}")
set(PINKCOIN_VERSION_MINOR "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define CLIENT_VERSION_REVISION[ \t]+([0-9]+)" _ "${CLIENTVERSION_CONTENT}")
set(PINKCOIN_VERSION_REVISION "${CMAKE_MATCH_1}")

string(REGEX MATCH "#define CLIENT_VERSION_BUILD[ \t]+([0-9]+)" _ "${CLIENTVERSION_CONTENT}")
set(PINKCOIN_VERSION_BUILD "${CMAKE_MATCH_1}")

# Validate extraction
if(NOT PINKCOIN_VERSION_MAJOR OR NOT PINKCOIN_VERSION_MINOR)
    message(FATAL_ERROR "Failed to extract version from clientversion.h")
endif()

# Set defaults for missing values
if(NOT PINKCOIN_VERSION_REVISION)
    set(PINKCOIN_VERSION_REVISION 0)
endif()
if(NOT PINKCOIN_VERSION_BUILD)
    set(PINKCOIN_VERSION_BUILD 0)
endif()

# Create version string
set(PINKCOIN_VERSION_STRING "${PINKCOIN_VERSION_MAJOR}.${PINKCOIN_VERSION_MINOR}.${PINKCOIN_VERSION_REVISION}")

message(STATUS "Pinkcoin version: ${PINKCOIN_VERSION_STRING}")
