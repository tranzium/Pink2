# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# LevelDB embedded build for Pinkcoin

# LevelDB core source files
set(LEVELDB_SOURCES
    db/builder.cc
    db/c.cc
    db/db_impl.cc
    db/db_iter.cc
    db/dbformat.cc
    db/filename.cc
    db/log_reader.cc
    db/log_writer.cc
    db/memtable.cc
    db/repair.cc
    db/table_cache.cc
    db/version_edit.cc
    db/version_set.cc
    db/write_batch.cc
    table/block.cc
    table/block_builder.cc
    table/filter_block.cc
    table/format.cc
    table/iterator.cc
    table/merger.cc
    table/table.cc
    table/table_builder.cc
    table/two_level_iterator.cc
    util/arena.cc
    util/bloom.cc
    util/cache.cc
    util/coding.cc
    util/comparator.cc
    util/crc32c.cc
    util/env.cc
    util/filter_policy.cc
    util/hash.cc
    util/histogram.cc
    util/logging.cc
    util/options.cc
    util/status.cc
)

# Platform-specific source files
if(WIN32)
    list(APPEND LEVELDB_SOURCES
        port/port_win.cc
        util/env_win.cc
    )
else()
    list(APPEND LEVELDB_SOURCES
        port/port_posix.cc
        util/env_posix.cc
    )
endif()

# Memenv helper library
set(MEMENV_SOURCES
    helpers/memenv/memenv.cc
)

# Create LevelDB static library
add_library(leveldb STATIC ${LEVELDB_SOURCES})

target_include_directories(leveldb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific definitions
if(WIN32)
    target_compile_definitions(leveldb
        PRIVATE
            LEVELDB_PLATFORM_WINDOWS
            OS_WIN
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
    target_link_libraries(leveldb PRIVATE shlwapi)
else()
    target_compile_definitions(leveldb
        PRIVATE
            LEVELDB_PLATFORM_POSIX
            OS_LINUX
    )
    target_link_libraries(leveldb PRIVATE Threads::Threads)
endif()

# Common definitions
target_compile_definitions(leveldb
    PRIVATE
        LEVELDB_IS_BIG_ENDIAN=0
)

# Disable RTTI and exceptions for smaller binary (optional, matches original)
if(NOT WIN32)
    target_compile_options(leveldb PRIVATE
        -fno-builtin-memcmp
    )
endif()

# Suppress warnings in third-party code
target_compile_options(leveldb PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
)

# Create memenv static library
add_library(memenv STATIC ${MEMENV_SOURCES})

target_include_directories(memenv
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific definitions for memenv (same as leveldb)
if(WIN32)
    target_compile_definitions(memenv
        PRIVATE
            LEVELDB_PLATFORM_WINDOWS
            OS_WIN
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
else()
    target_compile_definitions(memenv
        PRIVATE
            LEVELDB_PLATFORM_POSIX
            OS_LINUX
    )
endif()

target_link_libraries(memenv PUBLIC leveldb)

# Suppress warnings in memenv
target_compile_options(memenv PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
)

# Export targets for parent project
set(LEVELDB_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    PARENT_SCOPE
)
