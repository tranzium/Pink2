# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Pinkcoin core library and daemon

#-----------------------------------------
# Core source files (shared between daemon and GUI)
#-----------------------------------------
set(PINKCOIN_CORE_SOURCES
    addrman.cpp
    alert.cpp
    bitcoinrpc.cpp
    blockprocessing.cpp
    checkpoints.cpp
    crypter.cpp
    db.cpp
    kernel.cpp
    key.cpp
    keystore.cpp
    main.cpp
    mempool.cpp
    rewards.cpp
    miner.cpp
    net.cpp
    netbase.cpp
    ntp.cpp
    pbkdf2.cpp
    protocol.cpp
    rpcblockchain.cpp
    rpcdump.cpp
    rpcmining.cpp
    rpcnet.cpp
    rpcrawtransaction.cpp
    rpcsmessage.cpp
    rpc_wallet_keys.cpp
    rpc_wallet_mgmt.cpp
    rpc_wallet_query.cpp
    rpc_wallet_send.cpp
    rpcwallet.cpp
    script.cpp
    scrypt.cpp
    smessage.cpp
    stakedb.cpp
    stealth.cpp
    sync.cpp
    txdb-leveldb.cpp
    util.cpp
    validation.cpp
    version.cpp
    wallet.cpp
    walletdb.cpp
)

set(PINKCOIN_CORE_HEADERS
    addrman.h
    alert.h
    allocators.h
    base58.h
    bignum.h
    bitcoinrpc.h
    checkpoints.h
    clientversion.h
    coincontrol.h
    compat.h
    crypter.h
    db.h
    init.h
    kernel.h
    key.h
    keystore.h
    main.h
    miner.h
    mruset.h
    net.h
    netbase.h
    ntp.h
    pbkdf2.h
    protocol.h
    rpcwallet_util.h
    script.h
    scrypt.h
    serialize.h
    smessage.h
    stakedb.h
    stealth.h
    sync.h
    threadsafety.h
    txdb.h
    txdb-leveldb.h
    ui_interface.h
    uint256.h
    util.h
    version.h
    wallet.h
    walletdb.h
)

#-----------------------------------------
# Assembly files for scrypt (platform-specific)
#-----------------------------------------
# Assembly handling varies by platform and architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(SCRYPT_ASM_SOURCE scrypt-x86_64.S)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i.86|x86")
    set(SCRYPT_ASM_SOURCE scrypt-x86.S)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64|AARCH64")
    set(SCRYPT_ASM_SOURCE scrypt-arm.S)
endif()

# Enable assembly language if we have assembly sources
if(SCRYPT_ASM_SOURCE)
    enable_language(ASM)
    list(APPEND PINKCOIN_CORE_SOURCES ${SCRYPT_ASM_SOURCE})
endif()

#-----------------------------------------
# Core library (shared between daemon and GUI)
#-----------------------------------------
add_library(pinkcoin_core STATIC
    ${PINKCOIN_CORE_SOURCES}
    ${PINKCOIN_CORE_HEADERS}
)

# Disable Qt auto-processing for this non-Qt target
# (ui_interface.h is not a Qt UI file despite its name)
set_target_properties(pinkcoin_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Add generated build.h directory to include path
target_include_directories(pinkcoin_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/generated
    PRIVATE
        ${LEVELDB_INCLUDE_DIRS}
)

# Link dependencies
target_link_libraries(pinkcoin_core
    PUBLIC
        json_spirit
        leveldb
        memenv
        $<$<TARGET_EXISTS:Boost::system>:Boost::system>
        $<$<TARGET_EXISTS:Boost::chrono>:Boost::chrono>
        Boost::program_options
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        BerkeleyDB::BerkeleyDB
        Libsecp256k1::Libsecp256k1
        ZLIB::ZLIB
        Threads::Threads
)

# Optional UPnP support
if(USE_UPNP AND MiniUPnPc_FOUND)
    target_link_libraries(pinkcoin_core PUBLIC MiniUPnPc::MiniUPnPc)
endif()

# Platform-specific libraries
if(WIN32)
    target_link_libraries(pinkcoin_core
        PUBLIC
            ws2_32
            mswsock
            shlwapi
            ole32
            oleaut32
            uuid
            gdi32
            crypt32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(pinkcoin_core
        PUBLIC
            rt
            dl
    )
elseif(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(APPSERVICES_FRAMEWORK ApplicationServices)
    find_library(APPKIT_FRAMEWORK AppKit)
    target_link_libraries(pinkcoin_core
        PUBLIC
            ${FOUNDATION_FRAMEWORK}
            ${APPSERVICES_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
    )
endif()

# Ensure build.h is generated before compiling
add_dependencies(pinkcoin_core generate_build_info)

#-----------------------------------------
# Daemon executable (pink2d)
#-----------------------------------------
if(BUILD_DAEMON)
    add_executable(pink2d
        init.cpp
        noui.cpp
    )

    # Disable Qt auto-processing for this non-Qt target
    set_target_properties(pink2d PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )

    target_link_libraries(pink2d
        PRIVATE
            pinkcoin_core
    )

    # Set output name
    set_target_properties(pink2d PROPERTIES
        OUTPUT_NAME pink2d
    )

    # Windows-specific settings
    if(WIN32)
        # Ensure console application
        set_target_properties(pink2d PROPERTIES
            WIN32_EXECUTABLE FALSE
        )
    endif()
endif()
