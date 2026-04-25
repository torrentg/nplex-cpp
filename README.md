# nplex-cpp

The official C++20 client library for [nplex](https://github.com/torrentg/nplex) — a reactive,
key-value database with ACID guarantees designed for scenarios where low-latency reads,
real-time change notifications, and minimal resource consumption matter.

## Features

* **High performance** — A single client can sustain >40,000 commits/s on modest hardware (NUC7i7BNH).
* **Blazing-fast reads** — Data lives in client memory; reads never hit the server.
* **Multi-thread support** — Multiple threads can create and submit transactions concurrently.
* **Three isolation levels** — `READ_COMMITTED`, `REPEATABLE_READ`, and `SERIALIZABLE`.
* **Reactor pattern** — Register a reactor to receive real-time change notifications on every commit.
* **ACID transactions** — Atomic, Consistent, Isolated, and Durable.
* **Flexible connection** — Automatic reconnection and configurable timeout/back-pressure parameters.
* **Comprehensive tests** — Unit tests, covering messaging, transactions, storage, address parsing, and more.

## Building

### Prerequisites

Install the required shared libraries before building:

* [libuv](https://github.com/libuv/libuv) — cross-platform asynchronous I/O.
* [{fmt}](https://github.com/fmtlib/fmt) — string formatting library.
* [flatbuffers](https://github.com/google/flatbuffers) — memory-efficient serialization library (including the `flatc` compiler).


### Compilation

```bash
mkdir -p build
cd build

# Normal build (Release)
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Other Targets

```bash
# Debug build (default)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Run unit tests
ctest -V

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -DENABLE_SANITIZERS=ON -DENABLE_THREAD_SANITIZER=OFF ..
make -j$(nproc)
ctest -V

# ThreadSanitizer
cmake -DENABLE_SANITIZERS=OFF -DENABLE_THREAD_SANITIZER=ON ..
make -j$(nproc)
ctest -V

# Profiling
cmake -DENABLE_PROFILER=ON ..
make -j$(nproc)
./example1
gprof ./example1 gmon.out

# Coverage
cmake -DENABLE_COVERAGE=ON ..
make -j$(nproc)
ctest -V

# Valgrind — memory check
valgrind --tool=memcheck --leak-check=yes ./example1

# Valgrind — thread check
valgrind --tool=helgrind --read-var-info=yes ./example1

# Count lines of code (excluding generated files)
cloc --exclude-content="automatically generated" include src test

# Static analysis
cppcheck --enable=all --inconclusive --std=c++20 --force \
  --suppress=missingIncludeSystem --suppress=unusedFunction \
  --suppress=useInitializationList --suppress=noExplicitConstructor \
  --suppress=cstyleCast -Ideps -Isrc -Iinclude src

# clang-tidy (uncomment CMAKE_CXX_CLANG_TIDY in CMakeLists.txt)
cmake ..
make -j$(nproc)
```

## Examples & Tools

| Binary | Type | Purpose |
|--------|------|---------|
| [`example1`](examples/example1.cpp) | Example | Minimal connection, read, ping and submit demo |
| [`functests`](examples/functests.cpp) | Tool | Functional tests for isolation levels |
| [`flooder`](examples/flooder.cpp) | Tool | Performance/load generator for write traffic |
| [`watcher`](examples/watcher.cpp) | Tool | Prints DB/session snapshots and streams updates in JSON |

All binaries support `-h` / `--help`, where the available parameters are documented.

## Dependencies

### Static

| Library | Description | License |
|---------|-------------|---------|
| [FastGlobbing](https://github.com/Robert-van-Engelen/FastGlobbing) | Wildcard pattern matching | CPOL |
| [cppcrc](https://github.com/DarrenLevine/cppcrc) | Header-only CRC generation | MIT |
| [cstring](https://github.com/torrentg/cstring) | Immutable C-string with reference counting | LGPL-3.0 |
| [cqueue](https://github.com/torrentg/cqueue) | Circular queue | LGPL-3.0 |
| [base64](https://github.com/tobiaslocker/base64) | Base64 encoder / decoder | MIT |
| [utf8.h](https://github.com/sheredom/utf8.h) | UTF-8 string functions | Unlicense |
| [doctest](https://github.com/doctest/doctest) | Testing framework | MIT license |


### Shared

| Library | Description | License |
|---------|-------------|---------|
| [{fmt}](https://github.com/fmtlib/fmt) | String formatting | MIT |
| [libuv](https://github.com/libuv/libuv) | Cross-platform async I/O | MIT |
| [flatbuffers](https://github.com/google/flatbuffers) | Efficient binary serialization | Apache-2.0 |

## Maintainers

This project is maintained by Gerard Torrent ([torrentg](https://github.com/torrentg/)).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
