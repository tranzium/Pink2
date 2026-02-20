# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Pinkcoin unit tests

#-----------------------------------------
# Test source files
#-----------------------------------------
set(PINKCOIN_TEST_SOURCES
    test_bitcoin.cpp
    accounting_tests.cpp
    allocator_tests.cpp
    base32_tests.cpp
    base58_tests.cpp
    base64_tests.cpp
    bignum_tests.cpp
    Checkpoints_tests.cpp
    DoS_tests.cpp
    getarg_tests.cpp
    key_tests.cpp
    miner_tests.cpp
    mruset_tests.cpp
    multisig_tests.cpp
    netbase_tests.cpp
    script_P2SH_tests.cpp
    script_tests.cpp
    serialize_tests.cpp
    sigopcount_tests.cpp
    transaction_tests.cpp
    uint160_tests.cpp
    uint256_tests.cpp
    util_tests.cpp
    wallet_tests.cpp
    string_utils_tests.cpp
    rpc_tests.cpp
    kernel_tests.cpp
    consensus_tests.cpp
    scrypt_tests.cpp
    crypter_tests.cpp
    walletdb_tests.cpp
    hash_tests.cpp
    stealth_tests.cpp
    block_tests.cpp
    stakedb_tests.cpp
    smessage_tests.cpp
    pbkdf2_tests.cpp
    mainnet_block_tests.cpp
    keystore_tests.cpp
    alert_tests.cpp
    addrman_tests.cpp
    rpcdump_tests.cpp
    checkblock_tests.cpp
    test_framework.cpp
    integration_tests.cpp
    pos_tests.cpp
    wallet_ops_tests.cpp
    init_tests.cpp
    rpc_framework_tests.cpp
    rpc_command_tests.cpp
    net_tests.cpp
    golden_tests.cpp
    protocol_tests.cpp
    rpc_blockchain_tests.cpp
    http_tests.cpp
    rpc_mining_tests.cpp
    db_tests.cpp
    ntp_tests.cpp
    rpc_coverage_tests.cpp
    consensus_pinning_tests.cpp
    edge_case_tests.cpp
    staking_tests.cpp
    logging_tests.cpp
    security_tests.cpp
)

#-----------------------------------------
# Test data files
#-----------------------------------------
set(PINKCOIN_TEST_DATA
    data/base58_encode_decode.json
    data/base58_keys_invalid.json
    data/script_invalid.json
    data/script_valid.json
    data/mainnet_blocks.json
)

#-----------------------------------------
# Additional sources needed for test linking
#-----------------------------------------
set(PINKCOIN_TEST_EXTRA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../noui.cpp
)

#-----------------------------------------
# Create test executable
#-----------------------------------------
add_executable(test_pinkcoin
    ${PINKCOIN_TEST_SOURCES}
    ${PINKCOIN_TEST_EXTRA_SOURCES}
)

# Disable Qt auto-processing for this non-Qt target
set_target_properties(test_pinkcoin PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(test_pinkcoin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(test_pinkcoin
    PRIVATE
        pinkcoin_core
        Boost::unit_test_framework
)

# Define BOOST_TEST_DYN_LINK if using shared Boost
if(NOT Boost_USE_STATIC_LIBS)
    target_compile_definitions(test_pinkcoin PRIVATE BOOST_TEST_DYN_LINK)
else()
    # For static Boost linking (especially on Windows/MXE), disable auto-linking
    target_compile_definitions(test_pinkcoin PRIVATE
        BOOST_ALL_NO_LIB
        BOOST_TEST_NO_LIB
    )
endif()

# Define TEST_DATA_DIR so tests can find JSON data files
target_compile_definitions(test_pinkcoin PRIVATE
    TEST_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/data
)

#-----------------------------------------
# Copy test data to build directory
#-----------------------------------------
foreach(DATA_FILE ${PINKCOIN_TEST_DATA})
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${DATA_FILE}
        ${CMAKE_CURRENT_BINARY_DIR}/${DATA_FILE}
        COPYONLY
    )
endforeach()

#-----------------------------------------
# Register with CTest
#-----------------------------------------
add_test(
    NAME pinkcoin_tests
    COMMAND test_pinkcoin --log_level=test_suite
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Set test properties
set_tests_properties(pinkcoin_tests PROPERTIES
    TIMEOUT 600
)
