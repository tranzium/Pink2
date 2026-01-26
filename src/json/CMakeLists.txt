# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# JSON Spirit library for Pinkcoin

set(JSON_SPIRIT_SOURCES
    json_spirit_reader.cpp
    json_spirit_value.cpp
    json_spirit_writer.cpp
)

set(JSON_SPIRIT_HEADERS
    json_spirit.h
    json_spirit_error_position.h
    json_spirit_reader.h
    json_spirit_reader_template.h
    json_spirit_stream_reader.h
    json_spirit_utils.h
    json_spirit_value.h
    json_spirit_writer.h
    json_spirit_writer_template.h
)

add_library(json_spirit STATIC ${JSON_SPIRIT_SOURCES} ${JSON_SPIRIT_HEADERS})

target_include_directories(json_spirit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# JSON Spirit requires Boost Spirit
target_link_libraries(json_spirit
    PUBLIC
        Boost::headers
)

# Suppress warnings in third-party code
target_compile_options(json_spirit PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
)
