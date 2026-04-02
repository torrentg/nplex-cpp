# nplex-cpp

The official C++20 client library for [nplex](https://github.com/torrentg/nplex) — a reactive,
key-value database with ACID guarantees designed for scenarios where low-latency reads,
real-time change notifications, and minimal resource consumption matter.

## Features

* **High performance** — A single client can sustain >40,000 commits/s on modest hardware (NUC7i7BNH).
* **Blazing-fast reads** — Data lives in client memory; reads never hit the server.
* **Multi-thread support** — Thread-safe client with a dedicated event-loop thread. Multiple threads can create and submit transactions concurrently.
* **Three isolation levels** — `READ_COMMITTED`, `REPEATABLE_READ`, and `SERIALIZABLE`, covering the full spectrum from maximum performance to full consistency.
* **Reactor pattern** — Register a reactor to receive real-time change notifications on every commit, enabling event-driven architectures.
* **ACID transactions** — Atomic, consistent, isolated, and durable. Forced mode available for privileged users.
* **Flexible connection** — Comma-separated server list with automatic reconnection and configurable timeout/back-pressure parameters.
* **Comprehensive tests** — Unit tests powered by [doctest](https://github.com/doctest/doctest), covering messaging, transactions, storage, address parsing, and more.

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

### Quality assurance

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

## Running example1

`example1` is a minimal demonstration that connects to an nplex server, prints the database
contents, measures round-trip latency with a ping, and submits a single transaction.

Edit the connection parameters at the top of `examples/example1.cpp` before building:

```cpp
nplex::params_t params = {
    .servers  = "localhost:14022",
    .user     = "admin",
    .password = "s3cr3t"
};
```

Start an nplex instance, then run:

```bash
./build/example1
```

## Running functests

`functests` exercises the three transaction isolation levels against a live nplex server and
reports any violations found.

```bash
./build/functests --user USER --password PWD --servers HOST:PORT
```

Options:

| Flag | Description |
|------|-------------|
| `-u`, `--user USER` | User identifier (mandatory) |
| `-p`, `--password PWD` | User password (mandatory) |
| `-s`, `--servers LIST` | Comma-separated server list, e.g. `localhost:14022` (mandatory) |
| `-h`, `--help` | Show help and exit |

## Running flooder

`flooder` is a performance-testing tool that hammers an nplex server with write transactions
and reports throughput statistics at regular intervals.

```bash
./build/flooder --user USER --password PWD --servers HOST:PORT [options]
```

Options:

| Flag | Description | Default |
|------|-------------|---------|
| `-u`, `--user USER` | User identifier (mandatory) | — |
| `-p`, `--password PWD` | User password (mandatory) | — |
| `-s`, `--servers LIST` | Comma-separated server list (mandatory) | — |
| `-n`, `--tx-per-second N` | Target transactions per second | 1 |
| `-m`, `--max-active-tx N` | Maximum concurrent in-flight transactions | 100 |
| `-k`, `--num-keys N` | Approximate number of managed keys | 100 |
| `-b`, `--data-size BYTES` | Average value size in bytes | 25 |
| `-r`, `--refresh SECS` | Statistics refresh interval in seconds | 1 |
| `-h`, `--help` | Show help and exit | — |

Output columns (one line per refresh interval): `TIME`, `#submits`, `#commits`, `#rejects`,
`avgtime` (µs), `#updates`, `#updkeys`, `#updbytes`.

## Dependencies

### Static

* [cppcrc](https://github.com/DarrenLevine/cppcrc). A very small, fast, header-only, C++ library for generating CRCs. MIT license.
* [cstring](https://github.com/torrentg/cstring). A C++ immutable C-string with reference counting. LGPL-3.0 license.
* [cqueue](https://github.com/torrentg/cqueue). A C++20 header-only circular queue container. LGPL-3.0 license.
* [doctest](https://github.com/doctest/doctest). The fastest feature-rich C++11/14/17/20/23 single-header testing framework. MIT license.
* [FastGlobbing](https://github.com/Robert-van-Engelen/FastGlobbing). Wildcard string matching and globbing library. CPOL license.
* [utf8.h](https://github.com/sheredom/utf8.h). Utf8 string functions for C and C++. Unlicense license.

### Shared

* [{fmt}](https://github.com/fmtlib/fmt). A string formatting library. MIT license.
* [libuv](https://github.com/libuv/libuv). Cross-platform asynchronous I/O. MIT license.
* [flatbuffers](https://github.com/google/flatbuffers). Memory efficient serialization library. Apache-2.0 license.

## Maintainers

This project is maintained by Gerard Torrent ([torrentg](https://github.com/torrentg/)).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
