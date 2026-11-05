## K8S Control System

[English](README.md)

Ambiente para avaliar controladores fuzzy implantados como serviços escaláveis horizontalmente no Kubernetes. As plantas (carro, motor CC, tanque) são simuladas em C e fecham a malha por requisições HTTP ao controlador, implementado em C++ (FuzzyLite) e Python (scikit-fuzzy).

```
 gerador de carga (M processos de planta)        cluster Kubernetes
 ┌──────────────────────────────┐         ┌──────────────────────────────────┐
 │ planta i                     │  HTTP   │ Service fuzzy-service (NodePort) │
 │  thread sim  (RK4, dt=10 ms) │  POST   │        │ kube-proxy              │
 │  thread ctrl ── e, ed ───────┼────────►│        ▼                         │
 │        ▲     ◄── s, pod ─────┼─────────│  K réplicas de fuzzy-ctrl-cpp    │
 │        └─ v = sat(v + s)     │         │  (FIM + log CSV por pod)         │
 │  CSV: uma linha por requisição│        │ fastcollector: CPU/rede por nó   │
 └──────────────────────────────┘         └──────────────────────────────────┘
```

| Caminho | Conteúdo |
|---------|----------|
| `plants/` | Simuladores das plantas (C). `common/` é o runtime compartilhado; `car/`, `dc_motor/`, `tank/` definem os modelos. |
| `controllers/fuzzy_cpp/` | Serviço do controlador, C++17. |
| `controllers/fuzzy_py/` | Controlador equivalente, Python. |
| `fuzzy-ctrl.yaml`, `collector.yaml` | Manifestos Kubernetes (controlador, coletor de métricas). |
| `scripts/config.env` | Ambiente das plantas usado por `make run_*`. |

Documentação: [arquitetura](docs/pt-BR/architecture.md) · [plantas](docs/pt-BR/plants.md) · [controlador](docs/pt-BR/controller.md) · [esquema dos dados](docs/pt-BR/data_schema.md) · [dependências](docs/pt-BR/dependencies.md)

### Início rápido

Requer `gcc`, `make`, cabeçalhos da libcurl, `cmake` ≥ 3.14 e compilador C++17 (o CMake baixa as dependências C++).

```bash
make all                                    # plantas + controlador C++
make run_ctrl                               # terminal 1: controlador na porta 5000
REQUEST_PERIOD_MS=10 DURATION_S=40 ./plants/bin/dc_motor 2 res_dc_motor_client1.csv   # terminal 2
```

### Kubernetes

```bash
make docker_cpp && docker push ramonvfs/fuzzy-ctrl-cpp:v3
kubectl apply -f fuzzy-ctrl.yaml -f collector.yaml
kubectl scale deployment fuzzy-ctrl-cpp --replicas=<K>
```

As plantas usam `CONTROLLER_URL=http://<ip-do-nó>:30005/control`. Os logs do controlador ficam em cada nó em `/tmp/dataset/fuzzy-ctrl/`. Com muitas plantas, ajuste antes o gerador de carga ([esgotamento de portas](docs/pt-BR/architecture.md#esgotamento-de-portas-efêmeras)).
