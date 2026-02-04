# Boost Migration Status

## Completed Migrations

### boost::filesystem → std::filesystem
- **Commit:** aaeed3b
- **Files:** 14 files migrated
- **Notes:**
  - `boost::filesystem::system_complete()` → `std::filesystem::absolute()`
  - `boost::filesystem::ifstream/ofstream` → `std::ifstream/std::ofstream`
  - Simplified copy_file options to `std::filesystem::copy_options::overwrite_existing`

### boost::bind → std::bind/lambdas
- **Commit:** 7a75336
- **Files:** init.cpp, bitcoinrpc.cpp, clientmodel.cpp/h, messagemodel.cpp/h, walletmodel.cpp/h, json_spirit_reader_template.h
- **Notes:**
  - Thread creation converted to lambdas
  - Signal binds converted to std::bind with connection-based disconnect
  - boost::function → std::function

### boost::chrono → std::chrono
- **Files:** ntp.cpp, rpcdump.cpp, util.h, CMakeLists.txt
- **Notes:**
  - `boost::chrono::duration_cast` → `std::chrono::duration_cast`
  - `boost::this_thread::sleep_for` → `std::this_thread::sleep_for`
  - `boost::date_time/posix_time` → std C++ time functions (std::tm, std::get_time, std::mktime)
  - Boost::chrono and Boost::system now OPTIONAL_COMPONENTS in CMake

### boost::shared_ptr → std::shared_ptr
- **Commit:** cf658da
- **Notes:** Direct replacement, std::shared_ptr is drop-in compatible

### BOOST_FOREACH → range-based for
- **Commit:** cf658da
- **Notes:** C++11 range-based for loops

### boost::tuple → std::tuple
- **Files:** miner.cpp, script.cpp, serialize.h, walletdb.cpp, test/multisig_tests.cpp
- **Notes:**
  - `boost::tuple` → `std::tuple`
  - `boost::get<N>(tuple)` → `std::get<N>(tuple)`
  - `tuple.get<N>()` → `std::get<N>(tuple)`
  - `boost::make_tuple` → `std::make_tuple`
  - Removed `using namespace boost;` where only used for tuples

### boost::variant → std::variant
- **Files:** script.h, script.cpp, base58.h, rpcwallet.cpp, wallet.cpp, qt/walletmodel.cpp, qt/coincontroldialog.cpp, rpcrawtransaction.cpp, test/base58_tests.cpp
- **Notes:**
  - `boost::variant<...>` → `std::variant<...>`
  - `boost::static_visitor<T>` → removed (std::visit uses duck typing)
  - `boost::apply_visitor(visitor, var)` → `std::visit(visitor, var)`
  - `boost::get<T>(&var)` → `std::get_if<T>(&var)`
  - `boost::get<T>(var)` → `std::get<T>(var)`
  - `var.type() == typeid(T)` → `std::holds_alternative<T>(var)`
  - **Exception:** json_spirit_value.h keeps boost::variant due to boost::recursive_wrapper dependency

### boost::lexical_cast → std::to_string/stoll
- **Files:** smessage.cpp, rpcsmessage.cpp
- **Notes:**
  - `boost::lexical_cast<std::string>(x)` → `std::to_string(x)`
  - `boost::lexical_cast<int64_t>(s)` → `std::stoll(s)`
  - Removed unused includes from bitcoinrpc.cpp, rpcwallet.cpp

## Remaining Low-Hanging Fruit

### boost::thread/* → std::thread/mutex
- **Notes:**
  - `boost::mutex` → `std::mutex`
  - `boost::recursive_mutex` → `std::recursive_mutex`
  - `boost::thread` → `std::thread`
  - `boost::condition_variable` → `std::condition_variable`

### boost::assign/list_of → initializer lists
- **Notes:** Replace `boost::assign::list_of(a)(b)(c)` with `{a, b, c}`

### boost::array → std::array
- **Notes:** Direct replacement

## Must Keep (No Standard Replacement)

### boost::asio
- **Reason:** No standard networking library until C++23 (and adoption is limited)
- **Includes:** boost/asio.hpp, boost/asio/ssl.hpp, boost/asio/ip/v6_only.hpp, boost/iostreams/*

### boost::signals2
- **Reason:** No standard signal/slot mechanism
- **Includes:** boost/signals2/signal.hpp, boost/signals2/connection.hpp, boost/signals2/last_value.hpp

### boost::spirit
- **Reason:** Complex parsing library, would require replacing entire JSON implementation
- **Includes:** Multiple boost/spirit/* headers

### boost::variant (in json_spirit)
- **Files:** src/json/json_spirit_value.h
- **Reason:** Uses boost::recursive_wrapper for recursive type definition
- **Note:** All other boost::variant usage has been migrated to std::variant

### boost::program_options
- **Reason:** No standard command-line parsing library

### boost::interprocess
- **Reason:** No standard IPC/file locking primitives

### boost::algorithm/string
- **Reason:** String algorithms (split, trim, to_lower, replace_all) - would need custom implementations

### boost::test
- **Reason:** Unit test framework, would require migration to different framework (e.g., Google Test)
