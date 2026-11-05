## K8S Control System

Este projeto oferece um ambiente integrado para simular sistemas dinâmicos com plantas escritas em C e controladores de lógica fuzzy implementados em C++ (FuzzyLite) e em Python (scikit-fuzzy).

O objetivo principal é avaliar o desempenho e a confiabilidade de controladores distribuídos em um ambiente Kubernetes. Com a lógica de controle empacotada em contêineres como microsserviços independentes, é possível analisar latência de rede, escalabilidade e tolerância a falhas em cenários de automação industrial. As simulações em C (carro, motor CC e tanque) servem como plantas de referência para avaliar como o controlador fuzzy lida com malhas de realimentação em tempo real quando implantado em um cluster distribuído.

```
 gerador de carga (M processos de planta)        cluster Kubernetes
 ┌──────────────────────────────┐         ┌──────────────────────────────────┐
 │ planta i                     │  HTTP   │ Service fuzzy-service (NodePort) │
 │  thread sim  (RK4, dt=10 ms) │  POST   │        │ kube-proxy              │
 │  thread ctrl ── e, ed ───────┼────────►│        ▼                         │
 │        ▲     ◄── s, pod ─────┼─────────│  K réplicas de fuzzy-ctrl-cpp    │
 │        └─ v = sat(v + s)     │         │  (FIM + log CSV por pod)         │
 │  CSV: res_<planta>_..._client│         │ fastcollector: CPU/rede por nó   │
 └──────────────────────────────┘         └──────────────────────────────────┘
```

### Estrutura do repositório

| Caminho | Conteúdo |
|---------|----------|
| `plants/` | Simuladores das plantas em C. `common/` contém o runtime compartilhado (threads, cliente HTTP, RK4, log CSV); `car/`, `dc_motor/` e `tank/` só definem o modelo de cada planta. |
| `controllers/fuzzy_cpp/` | Serviço do controlador fuzzy em C++17 (FuzzyLite + cpp-httplib), usado nos experimentos. |
| `controllers/fuzzy_py/` | Controlador equivalente em Python (scikit-fuzzy + Flask). |
| `fuzzy-ctrl.yaml` | Deployment e Service NodePort do controlador C++ no Kubernetes. |
| `collector.yaml` | DaemonSet `fastcollector`, que coleta métricas de CPU e rede dos nós. |
| `scripts/config.env` | Variáveis de ambiente usadas pelas plantas quando executadas pelo `make`. |
| `docs/` | Documentação detalhada (abaixo). |

### Documentação

| Documento | Conteúdo |
|-----------|----------|
| [docs/architecture.md](docs/architecture.md) | Componentes, malha de controle, modelo de threads e de temporização, implantação no Kubernetes, esgotamento de portas. |
| [docs/plants.md](docs/plants.md) | Modelos das plantas, parâmetros, perfis de setpoint, ganhos, configuração de execução. |
| [docs/controller.md](docs/controller.md) | Sistema de inferência fuzzy, API HTTP, concorrência, log, configuração. |
| [docs/data_schema.md](docs/data_schema.md) | Descrição coluna a coluna de cada CSV gerado e como cruzá-los. |
| [docs/dependencies.md](docs/dependencies.md) | Bibliotecas, versões e licenças. |

### Início rápido (local)

Requisitos: `gcc`, `make`, cabeçalhos de desenvolvimento da `libcurl`, `cmake` ≥ 3.14, compilador C++17 e acesso à internet na primeira configuração do CMake (as dependências são baixadas).

```bash
make all                       # plantas + controlador C++

make run_ctrl                  # terminal 1: controlador na porta 5000, log em ./Result_CSS_*.csv

# terminal 2: motor CC, setpoint por degraus, período de 10 ms, 40 s
REQUEST_PERIOD_MS=10 DURATION_S=40 ./plants/bin/dc_motor 2 res_dc_motor_client1.csv
```

Cada planta grava uma linha de CSV por requisição de controle, e o controlador grava uma linha por requisição atendida. As duas carregam o mesmo par `(id do cliente, seq)`, então cada requisição pode ser rastreada de ponta a ponta (veja [docs/data_schema.md](docs/data_schema.md)).

### Kubernetes

```bash
make docker_cpp && docker push ramonvfs/fuzzy-ctrl-cpp:v3
kubectl apply -f fuzzy-ctrl.yaml -f collector.yaml
kubectl scale deployment fuzzy-ctrl-cpp --replicas=<K>
```

Os logs do controlador são gravados em cada nó em `/tmp/dataset/fuzzy-ctrl/` (um arquivo por pod). As plantas apontam para o NodePort, por exemplo `CONTROLLER_URL=http://<ip-do-nó>:30005/control`.

Antes de rodar com muitas plantas, ajuste o gerador de carga conforme [docs/architecture.md](docs/architecture.md#esgotamento-de-portas-efêmeras-no-gerador-de-carga); sem isso, as requisições começam a falhar acima de ~470 conexões por segundo.
