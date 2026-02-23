# Standalone Asio 1.36.0 — header-only networking library
# https://github.com/chriskohlhoff/asio
add_library(standalone_asio INTERFACE)
target_include_directories(standalone_asio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone Asio requires these defines
target_compile_definitions(standalone_asio INTERFACE
    ASIO_STANDALONE
    ASIO_NO_DEPRECATED
)
