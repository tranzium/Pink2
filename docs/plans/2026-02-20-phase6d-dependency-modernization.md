# Phase 6D: Dependency Modernization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace unmaintained json_spirit with nlohmann/json, reduce Boost surface, add lightweight error handling pattern.

**Architecture:** All-or-nothing migration of the RPC type system (22 files share types from bitcoinrpc.h). Mechanical pattern replacement with 1,378 existing tests as regression safety net.

**Tech Stack:** nlohmann/json (header-only, C++17), std::function-based Signal<> class

---

## Build Strategy

The JSON migration is all-or-nothing: once bitcoinrpc.h types change, ALL RPC files must be updated before the code compiles. Build checkpoints:

| After Task | Build |
|-----------|-------|
| Task 1 | Linux only (verify nlohmann compiles) |
| Task 4 | Linux + Windows (all production code migrated) |
| Task 6 | Linux + Windows + tests (all code migrated, json_spirit removed) |
| Task 8 | Linux + Windows + tests (Boost + signals cleanup) |
| Task 10 | Final verification (both targets, all tests) |

---

## Migration Pattern Reference

Every RPC file follows these mechanical substitutions:

| Pattern | json_spirit | nlohmann::json |
|---------|-------------|----------------|
| Include | `#include "json/json_spirit_*.h"` | `#include <nlohmann/json.hpp>` |
| Namespace | `using namespace json_spirit;` | `using json = nlohmann::json;` |
| Value type | `Value` | `json` |
| Object type | `Object` | `json` (must be object) |
| Array type | `Array` | `json` (must be array) |
| Object create | `Object result;` | `json result;` (default = null, or `json::object()`) |
| Array create | `Array arr;` | `json arr = json::array();` |
| Add field | `result.push_back(Pair("k", v));` | `result["k"] = v;` |
| Add element | `arr.push_back(v);` | `arr.push_back(v);` |
| Get string | `.get_str()` | `.get<std::string>()` |
| Get int | `.get_int()` | `.get<int>()` |
| Get int64 | `.get_int64()` | `.get<int64_t>()` |
| Get double | `.get_real()` | `.get<double>()` |
| Get bool | `.get_bool()` | `.get<bool>()` |
| Get object | `.get_obj()` | (remove — json IS the object) |
| Get array | `.get_array()` | (remove — json IS the array) |
| Null check | `.type() == null_type` | `.is_null()` |
| String check | `.type() == str_type` | `.is_string()` |
| Object check | `.type() == obj_type` | `.is_object()` |
| Array check | `.type() == array_type` | `.is_array()` |
| Int check | `.type() == int_type` | `.is_number_integer()` |
| Real check | `.type() == real_type` | `.is_number_float()` |
| Bool check | `.type() == bool_type` | `.is_boolean()` |
| Find value | `find_value(obj, "key")` | `obj["key"]` (creates null if missing) |
| Find + check | `Value v = find_value(o,"k"); if(v.type()!=null_type)` | `if (o.contains("k")) { auto v = o["k"]; ... }` |
| Parse JSON | `read_string(str, val)` / `read_stream(ifs, val)` | `val = json::parse(str)` / `val = json::parse(ifs)` |
| Serialize | `write_string(val, false)` | `val.dump()` |
| Serialize pretty | `write_string(val, true)` | `val.dump(4)` |
| RPC error | `Object err; err.push_back(Pair("code",c)); err.push_back(Pair("message",m));` | `json err; err["code"] = c; err["message"] = m;` |
| Implicit Value() | `Value(someString)` or `Value(someInt)` | just `someString` or `someInt` (implicit json conversion) |

**Critical difference:** json_spirit::Object is `vector<Pair>` (insertion-ordered, allows duplicate keys). nlohmann::json object is `std::map<string, json>` (key-sorted, no duplicates). RPC responses may have different field order — this is fine per JSON spec (objects are unordered).

**find_value() migration detail:** json_spirit's `find_value()` returns a static null Value if key not found. The calling code typically checks `if (v.type() != null_type)`. With nlohmann, use `obj.contains("key")` for existence checks, or `obj.value("key", default)` for safe access with defaults.

---

### Task 1: Vendor nlohmann/json and update build system

**Files:**
- Create: `src/json/nlohmann/json.hpp` (vendored single header)
- Create: `src/json/nlohmann/json_fwd.hpp` (forward declarations)
- Modify: `src/CMakeLists.txt` (add include path)

**Steps:**

1. Download nlohmann/json single-include header (v3.11.3 or latest stable) from GitHub releases:
   ```
   https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
   ```
   Place at `src/json/nlohmann/json.hpp`.

   Also download `json_fwd.hpp` from:
   ```
   https://github.com/nlohmann/json/releases/download/v3.11.3/json_fwd.hpp
   ```
   Place at `src/json/nlohmann/json_fwd.hpp`.

2. Update `src/CMakeLists.txt` — the include path `src/` is already in `target_include_directories`, and the vendored header will be included as `#include "json/nlohmann/json.hpp"`, so no CMake change is needed for the include path. However, add a comment noting the vendored library version.

3. Create a minimal test in `src/test/json_tests.cpp` to verify nlohmann/json compiles and works:
   ```cpp
   #include <boost/test/unit_test.hpp>
   #include "json/nlohmann/json.hpp"

   using json = nlohmann::json;

   BOOST_AUTO_TEST_SUITE(json_tests)

   BOOST_AUTO_TEST_CASE(json_basic_types)
   {
       json j;
       j["string"] = "hello";
       j["number"] = 42;
       j["float"] = 3.14;
       j["bool"] = true;
       j["null"] = nullptr;
       j["array"] = json::array({1, 2, 3});

       BOOST_CHECK(j["string"].is_string());
       BOOST_CHECK_EQUAL(j["string"].get<std::string>(), "hello");
       BOOST_CHECK(j["number"].is_number_integer());
       BOOST_CHECK_EQUAL(j["number"].get<int>(), 42);
       BOOST_CHECK(j["float"].is_number_float());
       BOOST_CHECK(j["bool"].is_boolean());
       BOOST_CHECK(j["null"].is_null());
       BOOST_CHECK(j["array"].is_array());
       BOOST_CHECK_EQUAL(j["array"].size(), 3u);
   }

   BOOST_AUTO_TEST_CASE(json_object_construction)
   {
       json obj;
       obj["name"] = "pinkcoin";
       obj["version"] = 1;
       obj["active"] = true;

       BOOST_CHECK(obj.is_object());
       BOOST_CHECK_EQUAL(obj.size(), 3u);
       BOOST_CHECK(obj.contains("name"));
       BOOST_CHECK(!obj.contains("missing"));
   }

   BOOST_AUTO_TEST_CASE(json_array_construction)
   {
       json arr = json::array();
       arr.push_back("item1");
       arr.push_back(42);
       arr.push_back(true);

       BOOST_CHECK(arr.is_array());
       BOOST_CHECK_EQUAL(arr.size(), 3u);
       BOOST_CHECK_EQUAL(arr[0].get<std::string>(), "item1");
       BOOST_CHECK_EQUAL(arr[1].get<int>(), 42);
   }

   BOOST_AUTO_TEST_CASE(json_parse_serialize)
   {
       std::string input = R"({"key":"value","num":123})";
       json parsed = json::parse(input);

       BOOST_CHECK(parsed.is_object());
       BOOST_CHECK_EQUAL(parsed["key"].get<std::string>(), "value");
       BOOST_CHECK_EQUAL(parsed["num"].get<int>(), 123);

       std::string serialized = parsed.dump();
       json reparsed = json::parse(serialized);
       BOOST_CHECK(reparsed == parsed);
   }

   BOOST_AUTO_TEST_CASE(json_nested_access)
   {
       json obj;
       obj["inner"]["deep"] = 42;

       BOOST_CHECK(obj["inner"].is_object());
       BOOST_CHECK_EQUAL(obj["inner"]["deep"].get<int>(), 42);
   }

   BOOST_AUTO_TEST_CASE(json_contains_and_value)
   {
       json obj;
       obj["exists"] = "yes";

       BOOST_CHECK(obj.contains("exists"));
       BOOST_CHECK(!obj.contains("missing"));

       std::string val = obj.value("exists", std::string("default"));
       BOOST_CHECK_EQUAL(val, "yes");

       std::string def = obj.value("missing", std::string("default"));
       BOOST_CHECK_EQUAL(def, "default");
   }

   BOOST_AUTO_TEST_CASE(json_iteration)
   {
       json arr = json::array({10, 20, 30});
       int sum = 0;
       for (const auto& v : arr)
           sum += v.get<int>();
       BOOST_CHECK_EQUAL(sum, 60);
   }

   BOOST_AUTO_TEST_CASE(json_type_checks)
   {
       json s = "hello";
       json i = 42;
       json d = 3.14;
       json b = true;
       json n = nullptr;
       json a = json::array();
       json o = json::object();

       BOOST_CHECK(s.is_string());
       BOOST_CHECK(i.is_number_integer());
       BOOST_CHECK(d.is_number_float());
       BOOST_CHECK(b.is_boolean());
       BOOST_CHECK(n.is_null());
       BOOST_CHECK(a.is_array());
       BOOST_CHECK(o.is_object());
   }

   BOOST_AUTO_TEST_CASE(json_file_parse)
   {
       // Verify we can parse from an ifstream (same pattern as read_json)
       std::string jsonStr = R"([{"name":"test","value":1},{"name":"test2","value":2}])";
       json parsed = json::parse(jsonStr);

       BOOST_CHECK(parsed.is_array());
       BOOST_CHECK_EQUAL(parsed.size(), 2u);
       BOOST_CHECK_EQUAL(parsed[0]["name"].get<std::string>(), "test");
       BOOST_CHECK_EQUAL(parsed[1]["value"].get<int>(), 2);
   }

   BOOST_AUTO_TEST_SUITE_END()
   ```

4. Add `json_tests.cpp` to `src/test/CMakeLists.txt`.

5. Build Linux release, run tests. Verify json_tests pass.

**Commit:** `Phase 6D: Vendor nlohmann/json v3.11.3 + basic tests`

---

### Task 2: Migrate RPC core framework (bitcoinrpc.h + bitcoinrpc.cpp)

**Files:**
- Modify: `src/bitcoinrpc.h` — change all type declarations
- Modify: `src/rpc/bitcoinrpc.cpp` — migrate framework code
- Modify: `src/rpcwallet_util.h` — change type declarations

This is the critical task. Once bitcoinrpc.h types change, all downstream RPC files must be updated before the code compiles.

**Steps:**

1. **Modify `src/bitcoinrpc.h`:**
   - Replace json_spirit includes with:
     ```cpp
     #include "json/nlohmann/json.hpp"
     using json = nlohmann::json;
     ```
   - Remove the three json_spirit includes
   - Change all function declarations from `json_spirit::Value func(const json_spirit::Array&, bool)` to `json func(const json&, bool)`
   - Change `typedef json_spirit::Value(*rpcfn_type)(...)` to `typedef json(*rpcfn_type)(const json&, bool)`
   - Update any json_spirit type references (Object, Array, Value, Pair)
   - Keep the existing `JSONRPCError` function but change its internals to use `json` instead of `Object`/`Pair`
   - Update `AmountFromValue` signature if needed
   - Update `ValueFromAmount` signature if needed

2. **Modify `src/rpcwallet_util.h`:**
   - Remove `#include "json/json_spirit_value.h"`
   - Add `#include "json/nlohmann/json.hpp"` and `using json = nlohmann::json;`
   - Change function signatures from json_spirit types to json

3. **Modify `src/rpc/bitcoinrpc.cpp`:**
   - Remove `using namespace json_spirit;`
   - Add `using json = nlohmann::json;` if not already via bitcoinrpc.h
   - Migrate all Object/Array/Pair/Value usage per the pattern table
   - Migrate `read_string()` → `json::parse()`
   - Migrate `write_string()` → `.dump()`
   - Migrate `find_value()` calls
   - Migrate type checks (`.type() == X_type` → `.is_X()`)
   - Migrate error object construction in `JSONRPCError`, `JSONRPCExecBatch`, `HTTPReply`, etc.
   - The HTTP request parsing (`ReadHTTPMessage`, `ReadHTTPHeaders`) doesn't use JSON — leave as-is
   - The `ConvertTo` and `CRPCConvertParam` code needs migration (it uses Array)
   - The `tableRPC` dispatch code uses `params` as Array — change to json

**Do NOT build yet** — downstream RPC files still reference old types.

---

### Task 3: Migrate all RPC handler files

**Files (12 total):**
- Modify: `src/rpc/rpcblockchain.cpp`
- Modify: `src/rpc/rpcmining.cpp`
- Modify: `src/rpc/rpcnet.cpp`
- Modify: `src/rpc/rpcdump.cpp`
- Modify: `src/rpc/rpcrawtransaction.cpp`
- Modify: `src/rpc/rpcsmessage.cpp`
- Modify: `src/rpc/rpcwallet.cpp`
- Modify: `src/rpc/rpc_wallet_send.cpp`
- Modify: `src/rpc/rpc_wallet_keys.cpp`
- Modify: `src/rpc/rpc_wallet_query.cpp`
- Modify: `src/rpc/rpc_wallet_mgmt.cpp`
- Modify: `src/qt/rpcconsole.cpp`

**Steps:**

For EACH file, apply these mechanical changes:

1. Remove `using namespace json_spirit;` (if present)
2. Add `using json = nlohmann::json;` at file scope (after includes)
3. Remove any direct json_spirit includes (they come via bitcoinrpc.h)
4. Replace all `Object result;` with `json result;`
5. Replace all `Array arr;` with `json arr = json::array();`
6. Replace all `result.push_back(Pair("key", value));` with `result["key"] = value;`
7. Replace all `arr.push_back(value);` with `arr.push_back(value);` (same syntax!)
8. Replace all `.get_str()` with `.get<std::string>()`
9. Replace all `.get_int()` with `.get<int>()`
10. Replace all `.get_int64()` with `.get<int64_t>()`
11. Replace all `.get_real()` with `.get<double>()`
12. Replace all `.get_bool()` with `.get<bool>()`
13. Replace all `.get_obj()` — remove the call (json IS the object)
14. Replace all `.get_array()` — remove the call (json IS the array)
15. Replace all `.type() == null_type` with `.is_null()`
16. Replace all `.type() != null_type` with `!.is_null()` — but careful with `params[n].type() != null_type` which should become `!params[n].is_null()`
17. Replace all `.type() == str_type` with `.is_string()`
18. Replace all `.type() == obj_type` with `.is_object()`
19. Replace all `.type() == array_type` with `.is_array()`
20. Replace all `.type() == int_type` with `.is_number_integer()`
21. Replace all `.type() == real_type` with `.is_number_float()`
22. Replace all `.type() == bool_type` with `.is_boolean()`
23. Replace all `find_value(obj, "key")` calls — context-dependent:
    - If followed by type check: use `obj.contains("key")` + `obj["key"]`
    - If used directly: use `obj["key"]`
    - If in `find_value(obj, "key").get_str()`: use `obj["key"].get<std::string>()`
24. Remove any `#include <boost/variant/get.hpp>` (rpcdump.cpp)

**Special cases per file:**

- **rpcrawtransaction.cpp**: Uses complex nested JSON for `createrawtransaction` params — careful with `.get_obj()` removal on nested objects
- **rpcsmessage.cpp**: Heavy JSON usage for message listing — verify Array iteration patterns
- **rpc_wallet_mgmt.cpp**: Uses `AmountFromValue()` which internally accesses `.get_real()` — this was fixed in Task 2 via bitcoinrpc.cpp
- **rpcdump.cpp**: Remove `#include <boost/variant/get.hpp>` — no longer needed
- **rpcconsole.cpp** (Qt): Uses `write_string()` for display formatting — change to `.dump(4)` for pretty-printing

**Build Linux + Windows after this task.** All production code should compile.

**Commit:** `Phase 6D: Migrate RPC layer from json_spirit to nlohmann/json`

---

### Task 4: Migrate test infrastructure and all test files

**Files:**
- Modify: `src/test/script_tests.cpp` — contains `read_json()` implementation
- Modify: `src/test/base58_tests.cpp` — uses `read_json()` and json_spirit
- Modify: `src/test/mainnet_block_tests.cpp` — uses `read_json()` and json_spirit
- Modify: `src/test/rpc_framework_tests.cpp` — tests RPC JSON parsing
- Modify: `src/test/rpc_command_tests.cpp` — tests all RPC responses
- Modify: `src/test/rpc_coverage_tests.cpp` — tests all 100 RPC commands
- Modify: `src/test/rpc_blockchain_tests.cpp` — blockchain RPC tests
- Modify: `src/test/rpc_mining_tests.cpp` — mining RPC tests
- Modify: `src/test/rpc_tests.cpp` — RPC integration tests
- Modify: `src/test/logging_tests.cpp` — logging RPC tests
- Modify: `src/test/staking_tests.cpp` — staking RPC tests

**Steps:**

1. **Migrate `read_json()` in script_tests.cpp:**
   ```cpp
   // Old:
   #include "json/json_spirit_reader_template.h"
   using namespace json_spirit;
   Array read_json(const std::string& filename) {
       Value v;
       if (!read_stream(ifs, v)) ...
       if (v.type() != array_type) ...
       return v.get_array();
   }

   // New:
   #include "json/nlohmann/json.hpp"
   using json = nlohmann::json;
   json read_json(const std::string& filename) {
       json v = json::parse(ifs);
       if (!v.is_array()) ...
       return v;
   }
   ```

2. **Update `extern` declaration** in all files that use `read_json`:
   ```cpp
   // Old:
   extern Array read_json(const std::string& filename);

   // New:
   extern json read_json(const std::string& filename);
   ```

3. **For each test file**, apply the same mechanical substitutions as Task 3, plus:
   - json_spirit `Object` iteration pattern:
     ```cpp
     // Old:
     Object obj = bv.get_obj();
     std::string desc = find_value(obj, "description").get_str();

     // New:
     const json& obj = bv;
     std::string desc = obj["description"].get<std::string>();
     ```
   - json_spirit `Array` iteration pattern:
     ```cpp
     // Old:
     Array blocks = read_json("file.json");
     for (const Value& bv : blocks) { ... }

     // New:
     json blocks = read_json("file.json");
     for (const json& bv : blocks) { ... }
     ```
   - Type enum comparisons in tests:
     ```cpp
     // Old:
     BOOST_CHECK_EQUAL(result.type(), obj_type);

     // New:
     BOOST_CHECK(result.is_object());
     ```

4. **Special: rpc_framework_tests.cpp** tests JSON parsing/serialization directly:
   - `read_string()` tests → `json::parse()` tests
   - `write_string()` tests → `.dump()` tests
   - These tests verify the JSON library itself, so update assertions to match nlohmann behavior

5. Build Linux + Windows, run full test suite.

**Commit:** `Phase 6D: Migrate test suite from json_spirit to nlohmann/json`

---

### Task 5: Remove json_spirit source files and clean CMake

**Files:**
- Delete: `src/json/json_spirit.h`
- Delete: `src/json/json_spirit_error_position.h`
- Delete: `src/json/json_spirit_reader.h`
- Delete: `src/json/json_spirit_reader.cpp`
- Delete: `src/json/json_spirit_reader_template.h`
- Delete: `src/json/json_spirit_stream_reader.h`
- Delete: `src/json/json_spirit_utils.h`
- Delete: `src/json/json_spirit_value.h`
- Delete: `src/json/json_spirit_value.cpp`
- Delete: `src/json/json_spirit_writer.h`
- Delete: `src/json/json_spirit_writer.cpp`
- Delete: `src/json/json_spirit_writer_template.h`
- Delete: `src/json/LICENSE.txt`
- Modify: `src/json/CMakeLists.txt` (or delete if empty)
- Modify: `src/CMakeLists.txt` — remove json_spirit sources from build, remove `BOOST_SPIRIT_THREADSAFE` definition

**Steps:**

1. Remove all json_spirit source files listed above using `git rm`.

2. Update `src/json/CMakeLists.txt` — remove json_spirit .cpp files from the library target. If the json directory CMakeLists only built json_spirit, it can be simplified to just define the include path.

3. Update `src/CMakeLists.txt`:
   - Remove `json_spirit_reader.cpp`, `json_spirit_writer.cpp`, `json_spirit_value.cpp` from source lists
   - Remove `-DBOOST_SPIRIT_THREADSAFE` from compile definitions (no longer needed)

4. Verify no remaining `#include.*json_spirit` references in any source file.

5. Build Linux + Windows, run full test suite.

**Commit:** `Phase 6D: Remove json_spirit library (replaced by nlohmann/json)`

---

### Task 6: Remove unused Boost link dependencies

**Files:**
- Modify: `src/CMakeLists.txt` — remove Boost::thread, Boost::program_options links
- Modify: `CMakeLists.txt` (root) — remove program_options and thread from find_package REQUIRED
- Modify: `src/CMakeLists.txt` — remove `-DBOOST_THREAD_USE_LIB` and `-DBOOST_BIND_GLOBAL_PLACEHOLDERS` definitions if no longer needed

**Steps:**

1. **Audit current Boost find_package:**
   - Keep: `unit_test_framework` (tests), `system` (asio dependency)
   - Remove: `program_options` (unused), `thread` (migrated to std::thread)
   - Keep as optional: `chrono` (may be needed by asio)

2. **Update root CMakeLists.txt:**
   ```cmake
   find_package(Boost 1.55 REQUIRED COMPONENTS
       unit_test_framework
       OPTIONAL_COMPONENTS
       system
       chrono
   )
   ```

3. **Update `src/CMakeLists.txt` link libraries:**
   - Remove `Boost::program_options`
   - Remove `Boost::thread`
   - Keep `Boost::system` (needed by asio)

4. **Remove compile definitions:**
   - Remove `-DBOOST_THREAD_USE_LIB` (std::thread now, not boost::thread)
   - Keep `-DBOOST_BIND_GLOBAL_PLACEHOLDERS` only if boost::asio still triggers the warning; test removing it

5. Build Linux + Windows, run full test suite.

**Commit:** `Phase 6D: Remove unused Boost link dependencies (thread, program_options)`

---

### Task 7: Replace boost::signals2 with std::function-based Signal class

**Files:**
- Create: `src/signal.h` — lightweight Signal<> template
- Modify: `src/ui_interface.h` — replace boost::signals2 signals
- Modify: `src/qt/clientmodel.h` — replace connection storage
- Modify: `src/qt/clientmodel.cpp` — update signal connections
- Modify: `src/qt/walletmodel.h` — replace connection storage
- Modify: `src/qt/walletmodel.cpp` — update signal connections
- Modify: `src/qt/messagemodel.h` — replace connection storage
- Modify: `src/qt/messagemodel.cpp` — update signal connections
- Modify: `src/keystore.h` — remove unused boost/signals2 include
- Create: `src/test/signal_tests.cpp` — tests for Signal<>

**Steps:**

1. **Create `src/signal.h`:**
   ```cpp
   #ifndef SIGNAL_H
   #define SIGNAL_H

   #include <functional>
   #include <vector>
   #include <memory>
   #include <mutex>
   #include <algorithm>

   // Connection handle — allows disconnecting a slot
   class Connection {
       std::shared_ptr<bool> m_alive;
   public:
       Connection() : m_alive(std::make_shared<bool>(true)) {}
       void disconnect() { if (m_alive) *m_alive = false; }
       bool connected() const { return m_alive && *m_alive; }
       std::shared_ptr<bool> tracker() const { return m_alive; }
   };

   // Signal with void return — matches boost::signals2 usage pattern
   template<typename... Args>
   class Signal {
       struct Slot {
           std::function<void(Args...)> fn;
           std::shared_ptr<bool> alive;
       };
       std::vector<Slot> m_slots;
       mutable std::mutex m_mutex;
   public:
       Connection connect(std::function<void(Args...)> fn) {
           std::lock_guard<std::mutex> lock(m_mutex);
           Connection conn;
           m_slots.push_back({std::move(fn), conn.tracker()});
           return conn;
       }

       void operator()(Args... args) {
           std::vector<Slot> snapshot;
           {
               std::lock_guard<std::mutex> lock(m_mutex);
               // Remove dead slots
               m_slots.erase(
                   std::remove_if(m_slots.begin(), m_slots.end(),
                       [](const Slot& s) { return !*s.alive; }),
                   m_slots.end());
               snapshot = m_slots;
           }
           for (auto& slot : snapshot)
               if (*slot.alive) slot.fn(args...);
       }

       void disconnect_all() {
           std::lock_guard<std::mutex> lock(m_mutex);
           for (auto& slot : m_slots)
               *slot.alive = false;
           m_slots.clear();
       }
   };

   // Signal with bool return + last_value combiner (for ThreadSafeAskFee)
   template<typename... Args>
   class SignalLastValue {
       struct Slot {
           std::function<bool(Args...)> fn;
           std::shared_ptr<bool> alive;
       };
       std::vector<Slot> m_slots;
       mutable std::mutex m_mutex;
   public:
       Connection connect(std::function<bool(Args...)> fn) {
           std::lock_guard<std::mutex> lock(m_mutex);
           Connection conn;
           m_slots.push_back({std::move(fn), conn.tracker()});
           return conn;
       }

       bool operator()(Args... args) {
           std::vector<Slot> snapshot;
           {
               std::lock_guard<std::mutex> lock(m_mutex);
               m_slots.erase(
                   std::remove_if(m_slots.begin(), m_slots.end(),
                       [](const Slot& s) { return !*s.alive; }),
                   m_slots.end());
               snapshot = m_slots;
           }
           bool result = false;
           for (auto& slot : snapshot)
               if (*slot.alive) result = slot.fn(args...);
           return result;  // returns last slot's result (matches last_value combiner)
       }
   };

   #endif // SIGNAL_H
   ```

2. **Modify `src/ui_interface.h`:**
   - Replace `#include <boost/signals2/signal.hpp>` and `#include <boost/signals2/last_value.hpp>` with `#include "signal.h"`
   - Replace `boost::signals2::signal<void(...)>` with `Signal<...>`
   - Replace `boost::signals2::signal<bool(...), boost::signals2::last_value<bool>>` with `SignalLastValue<...>`

3. **Modify Qt model files:**
   - Replace `#include <boost/signals2/signal.hpp>` with `#include "signal.h"`
   - Replace `std::vector<boost::signals2::connection>` with `std::vector<Connection>`
   - Update `.disconnect()` calls (same API in our Connection class)

4. **Remove `#include <boost/signals2/signal.hpp>` from keystore.h** if unused.

5. **Write signal_tests.cpp** with tests for:
   - Basic signal fire/receive
   - Multiple slots
   - Connection disconnect
   - disconnect_all
   - SignalLastValue combiner
   - Thread safety (concurrent connect/fire)

6. Build Linux + Windows, run full test suite.

**Commit:** `Phase 6D: Replace boost::signals2 with std::function-based Signal class`

---

### Task 8: Introduce Result<T> error handling pattern

**Files:**
- Create: `src/result.h` — lightweight Result<T> template
- Create: `src/test/result_tests.cpp` — tests

**Steps:**

1. **Create `src/result.h`:**
   ```cpp
   #ifndef RESULT_H
   #define RESULT_H

   #include <string>
   #include <variant>
   #include <stdexcept>

   // Lightweight Result<T> for new code paths.
   // Use instead of bool+error string or throw for expected failures.
   template<typename T>
   class Result {
       std::variant<T, std::string> m_value;
   public:
       // Success constructors
       Result(T value) : m_value(std::move(value)) {}

       // Error constructor
       static Result Error(std::string msg) {
           Result r;
           r.m_value = std::move(msg);
           return r;
       }

       bool ok() const { return std::holds_alternative<T>(m_value); }
       explicit operator bool() const { return ok(); }

       const T& value() const {
           if (!ok()) throw std::runtime_error("Result::value() called on error: " + error());
           return std::get<T>(m_value);
       }

       T& value() {
           if (!ok()) throw std::runtime_error("Result::value() called on error: " + error());
           return std::get<T>(m_value);
       }

       const std::string& error() const {
           if (ok()) throw std::runtime_error("Result::error() called on success");
           return std::get<std::string>(m_value);
       }

       // Value-or-default
       T value_or(T default_val) const {
           return ok() ? std::get<T>(m_value) : std::move(default_val);
       }

   private:
       Result() = default;
   };

   // Specialization for void (success/failure without a value)
   template<>
   class Result<void> {
       std::string m_error;
       bool m_ok;
   public:
       Result() : m_ok(true) {}
       static Result Success() { return Result(); }
       static Result Error(std::string msg) {
           Result r;
           r.m_ok = false;
           r.m_error = std::move(msg);
           return r;
       }

       bool ok() const { return m_ok; }
       explicit operator bool() const { return m_ok; }
       const std::string& error() const {
           if (m_ok) throw std::runtime_error("Result::error() called on success");
           return m_error;
       }
   };

   #endif // RESULT_H
   ```

2. **Write result_tests.cpp** with tests for:
   - Success construction and value access
   - Error construction and error message
   - bool conversion
   - value_or default
   - Result<void> success/error
   - Throwing on wrong access (value() on error, error() on success)

3. Add to `src/CMakeLists.txt` (header-only, just needs test file added).

4. Build, test.

**Commit:** `Phase 6D: Add Result<T> error handling pattern`

---

### Task 9: Write Phase 6D tests for migration correctness

**Files:**
- Modify: `src/test/json_tests.cpp` — add migration correctness tests

**Steps:**

Add tests that verify the nlohmann/json integration matches previous json_spirit behavior:

1. **Serialization format tests:** Verify JSON output for key RPC response patterns:
   - `getinfo` response structure
   - `getblock` response structure
   - Array of objects response
   - Nested objects

2. **Round-trip tests:** Parse → serialize → parse, verify equality.

3. **Error handling tests:** Verify JSONRPCError produces correct JSON error objects.

4. **Edge case tests:**
   - Empty object/array serialization
   - Large integers (int64_t max/min)
   - Unicode strings
   - Nested arrays/objects
   - null values in objects

5. Build, test.

**Commit:** `Phase 6D: Add JSON migration correctness tests`

---

### Task 10: Final build verification

**Steps:**

1. Clean rebuild both targets:
   ```bash
   rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
   rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
   ```

2. Timestamp verification:
   ```bash
   echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
   echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
   echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
   ```

3. Full test suite:
   ```bash
   ./build/linux-release/src/test/test_pinkcoin --log_level=test_suite 2>&1 | tail -5
   ```

4. Count test cases — should be 1,378 + ~30 new = ~1,408+.

5. Verify no remaining json_spirit references:
   ```bash
   grep -r "json_spirit" src/ --include="*.cpp" --include="*.h" | grep -v "nlohmann"
   ```
   Should return empty.

6. Verify no remaining unused Boost includes:
   ```bash
   grep -r "boost/thread" src/ --include="*.cpp" --include="*.h"
   grep -r "boost/spirit" src/ --include="*.cpp" --include="*.h"
   grep -r "boost/variant" src/ --include="*.cpp" --include="*.h"
   ```
   All should return empty.

**Final commit (if not already committed):** Any remaining cleanup.
