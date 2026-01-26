# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Compiler and linker flags for security hardening

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)

#-----------------------------------------
# Warning flags
#-----------------------------------------
set(PINKCOIN_WARN_FLAGS
    -Wall
    -Wextra
    -Wno-ignored-qualifiers
    -Wformat
    -Wformat-security
    -Wno-unused-parameter
    -fdiagnostics-show-option
)

foreach(FLAG ${PINKCOIN_WARN_FLAGS})
    check_cxx_compiler_flag(${FLAG} HAVE_FLAG_${FLAG})
    if(HAVE_FLAG_${FLAG})
        add_compile_options(${FLAG})
    endif()
endforeach()

#-----------------------------------------
# Security hardening flags
#-----------------------------------------
if(ENABLE_HARDENING)
    # Stack protection
    if(NOT WIN32)
        # Workaround for Ubuntu gcc bug - disable default first
        check_cxx_compiler_flag(-fno-stack-protector HAVE_NO_STACK_PROTECTOR)
        if(HAVE_NO_STACK_PROTECTOR)
            add_compile_options(-fno-stack-protector)
        endif()

        # Enable full stack protection
        check_cxx_compiler_flag(-fstack-protector-all HAVE_STACK_PROTECTOR_ALL)
        if(HAVE_STACK_PROTECTOR_ALL)
            add_compile_options(-fstack-protector-all)
            add_compile_options(--param=ssp-buffer-size=1)
        endif()

        # Stack protector warning
        check_cxx_compiler_flag(-Wstack-protector HAVE_WSTACK_PROTECTOR)
        if(HAVE_WSTACK_PROTECTOR)
            add_compile_options(-Wstack-protector)
        endif()
    endif()

    # FORTIFY_SOURCE for release builds
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions(-D_FORTIFY_SOURCE=2)
    endif()

    # Position Independent Code (for ASLR)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Linker hardening
    if(PLATFORM_LINUX)
        # RELRO - Make GOT read-only
        check_linker_flag(CXX "-Wl,-z,relro" HAVE_RELRO)
        if(HAVE_RELRO)
            add_link_options(-Wl,-z,relro)
        endif()

        # BIND_NOW - Resolve all symbols at load time
        check_linker_flag(CXX "-Wl,-z,now" HAVE_BINDNOW)
        if(HAVE_BINDNOW)
            add_link_options(-Wl,-z,now)
        endif()
    endif()

    # Windows ASLR and DEP
    if(PLATFORM_WINDOWS)
        # Dynamic base (ASLR)
        check_linker_flag(CXX "-Wl,--dynamicbase" HAVE_DYNAMICBASE)
        if(HAVE_DYNAMICBASE)
            add_link_options(-Wl,--dynamicbase)
        endif()

        # NX compat (DEP)
        check_linker_flag(CXX "-Wl,--nxcompat" HAVE_NXCOMPAT)
        if(HAVE_NXCOMPAT)
            add_link_options(-Wl,--nxcompat)
        endif()

        # High entropy ASLR for 64-bit
        if(ARCH_64BIT)
            check_linker_flag(CXX "-Wl,--high-entropy-va" HAVE_HIGH_ENTROPY_VA)
            if(HAVE_HIGH_ENTROPY_VA)
                add_link_options(-Wl,--high-entropy-va)
            endif()
        endif()
    endif()
endif()

#-----------------------------------------
# Platform-specific flags
#-----------------------------------------

# Enable threading
if(NOT WIN32)
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

# 32-bit SSE2 support
if(ARCH_32BIT AND NOT PLATFORM_WINDOWS)
    check_cxx_compiler_flag(-msse2 HAVE_SSE2)
    if(HAVE_SSE2)
        add_compile_options(-msse2)
    endif()
endif()

# Windows-specific
if(PLATFORM_WINDOWS)
    add_definitions(-D_MT)
    add_definitions(-DBOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN)

    if(STATIC_BUILD)
        add_link_options(-static-libgcc -static-libstdc++ -static)
    endif()
endif()

# macOS-specific
if(PLATFORM_MACOS)
    add_definitions(-DMSG_NOSIGNAL=0)

    # Minimum macOS version
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum macOS version")

    # Ensure we build for the right architecture
    if(ARCH_64BIT)
        set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "macOS architectures")
    endif()
endif()

#-----------------------------------------
# Static linking configuration
#-----------------------------------------
if(STATIC_BUILD)
    if(PLATFORM_LINUX)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    elseif(PLATFORM_WINDOWS)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    endif()
endif()

#-----------------------------------------
# Debug/Release specific
#-----------------------------------------
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g")
