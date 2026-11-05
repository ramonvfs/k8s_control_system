# Dependencies

[Português](pt-BR/dependencies.md)

| Component | Dependency | Version | License |
|-----------|------------|---------|---------|
| Plants | GCC (C17) | tested 13.3 | GPL |
| | libcurl | ≥ 7.61 (tested 8.5.0) | curl |
| | glibc (pthreads, POSIX clocks, libm) | – | LGPL |
| C++ controller | [FuzzyLite](https://github.com/fuzzylite/fuzzylite) | 6.0 | GPL-3.0 or commercial |
| | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.42.0 | MIT |
| | [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT |
| | CMake (`FetchContent`), C++17 | ≥ 3.14 | BSD-3 |
| Python controller | Flask / Werkzeug | 3.1.3 / 3.1.8 | BSD-3 |
| | scikit-fuzzy | 0.5.0 | BSD-3 |
| | NumPy, SciPy, NetworkX | 2.2.6, 1.15.3, 3.4.2 | BSD-3 |
| Infrastructure | Kubernetes (Deployment, NodePort Service, DaemonSet) | – | Apache-2.0 |
| | `ramonvfs/fastcollector` | v1 (external) | – |

Images: C++ controller built on `alpine:3.19` (multi-stage, runtime ≈ 12 MB); Python controller on `python:3.10-alpine`. Plants on Debian/Ubuntu: `apt install build-essential libcurl4-openssl-dev`.
