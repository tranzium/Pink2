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
