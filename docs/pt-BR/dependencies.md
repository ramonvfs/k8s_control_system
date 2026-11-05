# Dependências

[English](../dependencies.md)

| Componente | Dependência | Versão | Licença |
|------------|-------------|--------|---------|
| Plantas | GCC (C17) | testado 13.3 | GPL |
| | libcurl | ≥ 7.61 (testado 8.5.0) | curl |
| | glibc (pthreads, relógios POSIX, libm) | – | LGPL |
| Controlador C++ | [FuzzyLite](https://github.com/fuzzylite/fuzzylite) | 6.0 | GPL-3.0 ou comercial |
| | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.42.0 | MIT |
| | [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT |
| | CMake (`FetchContent`), C++17 | ≥ 3.14 | BSD-3 |
| Controlador Python | Flask / Werkzeug | 3.1.3 / 3.1.8 | BSD-3 |
| | scikit-fuzzy | 0.5.0 | BSD-3 |
| | NumPy, SciPy, NetworkX | 2.2.6, 1.15.3, 3.4.2 | BSD-3 |
| Infraestrutura | Kubernetes (Deployment, Service NodePort, DaemonSet) | – | Apache-2.0 |
| | `ramonvfs/fastcollector` | v1 (externa) | – |

Imagens: controlador C++ sobre `alpine:3.19` (multi-stage, imagem final ≈ 12 MB); controlador Python sobre `python:3.10-alpine`. Plantas no Debian/Ubuntu: `apt install build-essential libcurl4-openssl-dev`.
