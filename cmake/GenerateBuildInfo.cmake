# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Generate build information from git

find_package(Git QUIET)

function(generate_build_info OUTPUT_DIR)
    set(BUILD_H_FILE "${OUTPUT_DIR}/build.h")

    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        # Get git commit hash
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=9 HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT_ID
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        # Check if tree is dirty
        execute_process(
            COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_DIRTY
            ERROR_QUIET
        )

        if(GIT_DIRTY)
            set(GIT_COMMIT_ID "${GIT_COMMIT_ID}-dirty")
        endif()

        # Get commit date
        execute_process(
            COMMAND ${GIT_EXECUTABLE} log -n 1 --format=%ci
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT_DATE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Generate build.h content
    if(GIT_COMMIT_ID)
        set(BUILD_H_CONTENT "#define GIT_COMMIT_ID \"${GIT_COMMIT_ID}\"\n#define BUILD_DATE \"${GIT_COMMIT_DATE}\"\n")
    else()
        set(BUILD_H_CONTENT "// No build information available\n")
    endif()

    # Only write if content changed
    if(EXISTS "${BUILD_H_FILE}")
        file(READ "${BUILD_H_FILE}" EXISTING_CONTENT)
        if("${EXISTING_CONTENT}" STREQUAL "${BUILD_H_CONTENT}")
            return()
        endif()
    endif()

    file(WRITE "${BUILD_H_FILE}" "${BUILD_H_CONTENT}")
    message(STATUS "Generated build.h with commit: ${GIT_COMMIT_ID}")
endfunction()

# Create build info at configure time
set(BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BUILD_INFO_DIR}")
generate_build_info("${BUILD_INFO_DIR}")

# Add custom target to regenerate at build time
add_custom_target(generate_build_info
    COMMAND ${CMAKE_COMMAND}
        -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
        -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT_DIR=${BUILD_INFO_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfoScript.cmake
    COMMENT "Updating build information"
)

# Create the script that will be run at build time
file(WRITE "${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfoScript.cmake" [=[
# Script to generate build.h at build time
find_package(Git QUIET)

set(BUILD_H_FILE "${OUTPUT_DIR}/build.h")

if(GIT_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=9 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY
        ERROR_QUIET
    )

    if(GIT_DIRTY)
        set(GIT_COMMIT_ID "${GIT_COMMIT_ID}-dirty")
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -n 1 --format=%ci
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(GIT_COMMIT_ID)
    set(BUILD_H_CONTENT "#define GIT_COMMIT_ID \"${GIT_COMMIT_ID}\"\n#define BUILD_DATE \"${GIT_COMMIT_DATE}\"\n")
else()
    set(BUILD_H_CONTENT "// No build information available\n")
endif()

if(EXISTS "${BUILD_H_FILE}")
    file(READ "${BUILD_H_FILE}" EXISTING_CONTENT)
    if("${EXISTING_CONTENT}" STREQUAL "${BUILD_H_CONTENT}")
        return()
    endif()
endif()

file(WRITE "${BUILD_H_FILE}" "${BUILD_H_CONTENT}")
]=])
