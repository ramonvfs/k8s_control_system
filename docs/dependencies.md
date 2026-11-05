# Dependências

## Plantas (C)

| Dependência | Versão | Licença | Uso |
|-------------|--------|---------|-----|
| GCC | C17 (padrão do `gcc`), testado com 13.3 | GPL (toolchain) | Compilador. |
| libcurl | pacote do sistema (testado com 8.5.0; exige ≥ 7.61 para as medições `*_TIME_T`) | curl (semelhante à MIT) | Cliente HTTP das requisições ao controlador e detalhamento dos tempos. |
| POSIX threads | glibc | LGPL | Threads de simulação e de controle, mutex. |
| Relógios POSIX | glibc | LGPL | `clock_nanosleep`, `CLOCK_MONOTONIC`, `CLOCK_REALTIME`. |
| libm | glibc | LGPL | `sqrt` (tanque). |

Compilação: `make plants` (flags `-Wall -Wextra -O2`). No Debian/Ubuntu: `apt install build-essential libcurl4-openssl-dev`.

## Controlador C++

| Dependência | Versão | Licença | Uso |
|-------------|--------|---------|-----|
| [FuzzyLite](https://github.com/fuzzylite/fuzzylite) | 6.0 | GPL-3.0 (ou licença comercial) | Motor de inferência fuzzy. Compilado a partir do código-fonte como biblioteca estática. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.42.0 | MIT | Servidor HTTP (só cabeçalho). |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | Leitura e escrita de JSON (só cabeçalho). |
| CMake | ≥ 3.14 | BSD-3 | Compilação; baixa as três bibliotecas acima com `FetchContent`. |
| Padrão C++ | C++17 | – | `std::clamp`, `<chrono>`, `<thread>`. |

Imagem de contêiner (`controllers/fuzzy_cpp/Dockerfile`): compilação multi-stage sobre `alpine:3.19` (`build-base`, `cmake`, `linux-headers`); a imagem final contém só o binário, `libstdc++` e `libgcc` (~12 MB).

## Controlador Python

De `controllers/fuzzy_py/requirements.txt` (imagem base `python:3.10-alpine`):

| Pacote | Versão | Licença | Uso |
|--------|--------|---------|-----|
| Flask | 3.1.3 | BSD-3 | Servidor HTTP (servidor de desenvolvimento, Werkzeug). |
| Werkzeug | 3.1.8 | BSD-3 | Servidor WSGI usado pelo Flask. |
| scikit-fuzzy | 0.5.0 | BSD-3 | Inferência fuzzy (`skfuzzy.control`). |
| NumPy | 2.2.6 | BSD-3 | Universo de discurso / vetores. |
| SciPy | 1.15.3 | BSD-3 | Dependência do scikit-fuzzy. |
| NetworkX | 3.4.2 | BSD-3 | Grafo de regras do scikit-fuzzy. |
| blinker, click, itsdangerous, Jinja2, MarkupSafe | veja o arquivo | BSD-3 | Dependências do Flask. |

## Infraestrutura

| Componente | Versão / imagem | Uso |
|------------|-----------------|-----|
| Kubernetes | Deployment (`apps/v1`), Service NodePort, DaemonSet, RBAC | Orquestração e escalonamento do controlador. |
| `ramonvfs/fuzzy-ctrl-cpp` | `v3` (este código) | Imagem do controlador referenciada em `fuzzy-ctrl.yaml`. |
| `ramonvfs/fastcollector` | `v1` (externa; código-fonte fora deste repositório) | Métricas de CPU e rede por nó, via cgroups e `/proc`. |
