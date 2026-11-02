# Dependencies

## Plants (C)

| Dependency | Version | License | Use |
|------------|---------|---------|-----|
| GCC | C17 (`gcc` default), tested with 13.3 | GPL (toolchain) | Compiler. |
| libcurl | system package (tested 8.5.0; needs ≥ 7.61 for `*_TIME_T` timing info) | curl (MIT-like) | HTTP client for the controller requests and timing breakdown. |
| POSIX threads | glibc | LGPL | Simulation and control threads, mutex. |
| POSIX clocks | glibc | LGPL | `clock_nanosleep`, `CLOCK_MONOTONIC`, `CLOCK_REALTIME`. |
| libm | glibc | LGPL | `sqrt` (tank). |

Build: `make plants` (flags `-Wall -Wextra -O2`). On Debian/Ubuntu: `apt install build-essential libcurl4-openssl-dev`.

## C++ controller

| Dependency | Version | License | Use |
|------------|---------|---------|-----|
| [FuzzyLite](https://github.com/fuzzylite/fuzzylite) | 6.0 | GPL-3.0 (or commercial license) | Fuzzy inference engine. Compiled from source as a static library. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.42.0 | MIT | HTTP server (header-only). |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | JSON parsing and serialization (header-only). |
| CMake | ≥ 3.14 | BSD-3 | Build; downloads the three libraries above with `FetchContent`. |
| C++ standard | C++17 | – | `std::clamp`, `<chrono>`, `<thread>`. |

Container image (`controllers/fuzzy_cpp/Dockerfile`): multi-stage build on `alpine:3.19` (`build-base`, `cmake`, `linux-headers`); the runtime image contains only the binary plus `libstdc++` and `libgcc`.

## Python controller

From `controllers/fuzzy_py/requirements.txt` (base image `python:3.10-alpine`):

| Package | Version | License | Use |
|---------|---------|---------|-----|
| Flask | 3.1.3 | BSD-3 | HTTP server (development server, Werkzeug). |
| Werkzeug | 3.1.8 | BSD-3 | WSGI server used by Flask. |
| scikit-fuzzy | 0.5.0 | BSD-3 | Fuzzy inference (`skfuzzy.control`). |
| NumPy | 2.2.6 | BSD-3 | Universe of discourse / arrays. |
| SciPy | 1.15.3 | BSD-3 | Dependency of scikit-fuzzy. |
| NetworkX | 3.4.2 | BSD-3 | Rule graph in scikit-fuzzy. |
| blinker, click, itsdangerous, Jinja2, MarkupSafe | see file | BSD-3 | Flask dependencies. |

## Infrastructure

| Component | Version / image | Use |
|-----------|-----------------|-----|
| Kubernetes | Deployment (`apps/v1`), Service NodePort, DaemonSet, RBAC | Orchestration and scaling of the controller. |
| `ramonvfs/fuzzy-ctrl-cpp` | `v3` (this code) | Controller image referenced by `fuzzy-ctrl.yaml`. |
| `ramonvfs/fastcollector` | `v1` (external, source not in this repository) | Per-node CPU/network metrics via cgroups and `/proc`. |
