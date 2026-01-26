# Pinkcoin CMake Migration Plan

## Original Project Brief

Welcome to Pinkcoin. This is an original colour coin (Pink), which may have been the first coin to have side-staking, which allows you to divide your wallet's stakes into fractions (percentages).

The wallet is aging and I want to bring it up to speed. Ideally, at first without changing any of the existing functionality, and we will work up-to chain changes, etc.

The original developer said to keep the DB version the same to ensure wallet.dat files remain backwards compatible, so at this time we need to peg that version and work around it.

Stealth addresses can be hidden and/or removed where it is safe to do so.

## Key Constraints

- **BerkeleyDB version**: Must remain pinned for wallet.dat backwards compatibility
- **Stealth addresses**: Can be hidden/removed where safe
- **No functionality changes initially**: Focus on build modernization first

## Migration Goals

1. Replace legacy Makefiles with CMake build system
2. Support cross-platform builds (Linux, Windows, macOS, Raspberry Pi)
3. Maintain all existing wallet functionality
4. Eventually modernize codebase and chain

## Build Targets

### Phase 1: CMake Build System (COMPLETE)

| Target | Preset | Status |
|--------|--------|--------|
| Linux Daemon (headless) | `linux-daemon-only` | COMPLETE |
| Windows Daemon (MXE cross-compile) | `windows-mxe-daemon` | COMPLETE |
| Windows GUI (MXE cross-compile) | `windows-mxe` | COMPLETE |
| Linux GUI | `linux-release` | COMPLETE |
| macOS GUI + Daemon | `macos-release` | READY (needs macOS hardware) |
| ARM64 (Raspberry Pi, etc.) | `linux-daemon-only` | Build natively on device |
| Unit Tests | `linux-tests` | PARTIAL (see notes) |

### Phase 2: Code Modernization (CURRENT)

**Scope: SAFE changes only - no consensus impact**

#### 2.1 C++ Syntax Modernization
- [x] `NULL` → `nullptr` (354 occurrences converted)
- [x] `.size() == 0` → `.empty()` (88 occurrences in safe files)
- [x] Raw loops → range-based for loops (17 conversions in safe files)
- [ ] Use `auto` for complex iterator types
- [ ] Consistent use of `const` and references

#### 2.2 UI Cleanup
- [x] Hide stealth address UI elements (checkbox hidden, addresses not loaded in UI)
- [ ] Remove dead/unused UI code
- [x] Qt modernization (deprecated algorithms and foreach macro replaced)

#### 2.3 Code Quality
- [x] Fix compiler warnings (SAFE warnings fixed; OpenSSL 3.0 deprecations deferred)
- [ ] Remove unused variables/includes
- [ ] Improve error messages and logging

#### 2.4 Documentation
- [ ] Code comments where logic is unclear
- [ ] Developer documentation

#### DO NOT TOUCH (Consensus Critical)
- `main.cpp` - block validation
- `kernel.cpp` - PoS/staking rules
- `script.cpp` - script execution
- Reward calculations
- Difficulty adjustment
- Checkpoint logic

### Phase 2: CAUTION Items (Deferred)

These require careful review if pursued later:
- Smart pointers (`new/delete` → `unique_ptr/shared_ptr`)
- Dependency API updates (Boost/OpenSSL)
- Complete removal of stealth code (vs just hiding UI)

### Phase 3: Chain Changes (FUTURE)

- To be determined
- Any consensus changes require extensive testing
- Staking rules must remain unchanged unless intentional fork

## Notes on CI/CD (Azure Pipelines)

The Azure Pipelines configuration was used historically because:
- Could not build Windows applications locally (cross-compilation issues)
- Could not sign Windows applications locally
- Similar constraints for macOS builds

With the new CMake + MXE setup, Windows cross-compilation now works locally from Linux. Azure Pipelines may still be useful for:
- Automated CI testing
- Release builds with code signing
- macOS builds (requires macOS runner)

These are **not** part of the current migration focus.

## Session Log

### 2026-01-24
- Confirmed Windows GUI build working with CMake + MXE
- Starting Linux GUI build
- Plan to test macOS afterwards for thoroughness
- Linux GUI verified working (`Pinkcoin-Qt v2.4.0.0-unk Riley's Revenge`)
- macOS CMake configuration reviewed - complete, awaiting hardware test
- ARM64 cross-compile: deferred - build natively on device instead
- Removed 32-bit support (ARM32, x86) - focusing on 64-bit platforms only

### 2026-01-25
- Phase 1 complete - CMake build system working for all major platforms
- Defined Phase 2 scope: SAFE changes only (no consensus impact)
- Starting Phase 2: C++ modernization, UI cleanup, code quality
- Unit tests run - core functionality passes, Bitcoin-specific test data fails
- Fixed TEST_DATA_DIR for test JSON files
- **Compiler Warning Fixes (Phase 2.3):**
  - Fixed `wallet.cpp`: misleading indentation in CreateCoinStake
  - Fixed `smessage.cpp`: bitwise OR bug (`||` → `|`) - actual bug fix
  - Fixed `script.h`: added explicit copy assignment operator (eliminated ~60 deprecated-copy warnings)
  - Fixed `serialize.h`: suppressed IMPLEMENT_SERIALIZE unused variables (eliminated ~10,544 warnings)
  - Fixed `checkpoints.cpp`: range-based for loop copy (`const string` → `const string&`)
  - Fixed `messagemodel.cpp`: implicit fallthrough in switch statement
  - Fixed `miner.cpp`: memset on non-trivial type (use `= {}` initialization)
- Remaining: 69 OpenSSL 3.0 deprecation warnings (CAUTION level - deferred)

### 2026-01-26
- **C++ Modernization (Phase 2.1):**
  - Converted 354 `NULL` → `nullptr` occurrences
  - Converted 88 `.size() == 0`/`.size() > 0`/`.size() != 0` → `.empty()`/`!.empty()` (safe files only)
- **UI Cleanup (Phase 2.2):**
  - Hidden stealth address checkbox in edit address dialog
  - Disabled stealth address loading in address table (backend still functional)
- **Qt Modernization (Phase 2.2):**
  - Replaced deprecated `qSort`, `qLowerBound`, `qUpperBound` with `std::sort`, `std::lower_bound`, `std::upper_bound`
  - Converted 20 `foreach` macros to C++11 range-based for loops
- **Housekeeping:**
  - Updated `.gitignore` for CMake build artifacts
  - Removed stale `build-win64/` directory (pre-preset test build)
- **C++ Modernization (Phase 2.1 continued):**
  - Converted 17 iterator-based for loops to range-based for loops
  - Files: ntp.cpp, bitcoinrpc.cpp, rpcdump.cpp, addrman.cpp, rpcsmessage.cpp, net.cpp, smessage.cpp, qt/guiutil.cpp, qt/transactiontablemodel.cpp
  - Skipped: Loops that modify iterator during iteration (erase patterns)
- **Dead Code Removal (Phase 2.3):**
  - Added `CAddrMan::empty()` method for API consistency
  - Removed ~100 lines of dead/commented code:
    - `bitcoinrpc.cpp`: Removed unused `specialOutput()` function (contained bug)
    - `bitcoinrpc.h`: Removed matching commented declarations
    - `rpcwallet.cpp`: Removed old account balance code, unreachable code after returns
    - `addrman.cpp`: Removed commented debug printf statements
    - `wallet.cpp`: Removed deprecated constant comments
    - `init.cpp`: Removed old `CTxDB().Close()` comment
    - `ntp.cpp`: Removed duplicate include comment
  - Note: Kept `util.h` template usage examples (useful documentation)

## macOS Build Notes

The macOS configuration is complete in CMake but requires actual macOS hardware to test.

### Dependencies (install via Homebrew)
```bash
brew install cmake ninja boost berkeley-db@4 openssl@3 qt@5 miniupnpc qrencode
```

### Build commands (on macOS)
```bash
cmake --preset macos-release
cmake --build build/macos-release
```

### Known considerations
- OpenSSL: macOS deprecated LibreSSL, use Homebrew's OpenSSL
- BerkeleyDB: Must use version 4.8 for wallet.dat compatibility
- Qt5: Use Homebrew's Qt5
- Code signing: Requires Apple Developer certificate for distribution

### Info.plist version
The `share/qt/Info.plist` has hardcoded version strings (2.3.0.0).
CMake sets bundle properties but may not override all plist values.

## ARM64 Build Notes (Raspberry Pi 3/4/5, ARM servers)

**Recommendation:** Build natively on the device using the standard Linux preset.

Cross-compilation from x86_64 requires a complete ARM64 sysroot which Ubuntu doesn't provide in standard repos. Native builds are simpler and reliable.

### Native build on ARM64 device
```bash
# Install dependencies
sudo apt install build-essential cmake ninja-build \
    libboost-all-dev libssl-dev libdb++-dev zlib1g-dev

# Build daemon
cmake --preset linux-daemon-only
cmake --build build/linux-daemon-only
```

### Future: Cross-compilation
If cross-compilation becomes needed, options include:
- Docker + QEMU emulation
- Custom ARM64 sysroot
- Linaro or other ARM toolchains with bundled libraries

The `arm64-cross` preset exists but requires manual sysroot setup.

## Unit Test Notes

Tests run with: `HOME=/tmp/empty_home ctest --output-on-failure`

### Passing Tests (core functionality)
- accounting, allocator, base32, base64, bignum
- DoS protection, getarg, mruset, netbase
- serialize, uint160, uint256, wallet coin selection
- Most util_tests

### Failing Tests (pre-existing issues)
These fail because test data was never updated from Bitcoin to Pinkcoin:
- **base58_tests**: JSON files contain Bitcoin addresses/WIF keys
- **Checkpoints_tests**: Hardcoded Bitcoin block hashes
- **key_tests**: Bitcoin key prefixes (0x80 vs Pinkcoin's)
- **script_tests**: Bitcoin transaction scripts in JSON
- **transaction_tests**: Bitcoin transaction test vectors
- **multisig_tests, sigopcount_tests, miner_tests**: Bitcoin-specific values

### Race Condition Failures
- util_loop_forever1/2: Timing-sensitive, may pass/fail randomly

### To Fix (Phase 2)
- Update `src/test/data/*.json` with Pinkcoin test vectors
- Update hardcoded checkpoint/key values in test files
- Consider removing Bitcoin-specific tests not applicable to Pinkcoin
