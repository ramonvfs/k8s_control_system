## K8S Control System

This project provides an integrated environment for simulating dynamic systems using C-based plants and Fuzzy Logic controllers implemented in both C++ (FuzzyLite) and Python (Scikit-Fuzzy).

The primary objective of this framework is to evaluate the performance and reliability of distributed controllers within a Kubernetes environment. By containerizing the control logic as independent microservices, the system allows for the analysis of network latency, scalability, and fault tolerance in industrial automation scenarios. The included C-based simulations (car, DC motor, and tank) serve as benchmark plants to validate how fuzzy controller handle real-time feedback loops when deployed across a distributed cluster.

```
 load generator (M plant processes)              Kubernetes cluster
 ┌──────────────────────────────┐         ┌──────────────────────────────────┐
 │ plant i                      │  HTTP   │ Service fuzzy-service (NodePort) │
 │  sim thread  (RK4, dt=10 ms) │  POST   │        │ kube-proxy              │
 │  ctrl thread ── e, ed ───────┼────────►│        ▼                         │
 │        ▲     ◄── s, pod ─────┼─────────│  K replicas of fuzzy-ctrl-cpp    │
 │        └─ v = sat(v + s)     │         │  (FIM + per-pod CSV log)         │
 │  CSV: res_<plant>_..._clientN│         │ fastcollector DaemonSet (CPU/net)│
 └──────────────────────────────┘         └──────────────────────────────────┘
```

### Repository layout

| Path | Content |
|------|---------|
| `plants/` | Plant simulators in C. `common/` holds the shared runtime (threads, HTTP client, RK4, CSV logging); `car/`, `dc_motor/`, `tank/` only define each model. |
| `controllers/fuzzy_cpp/` | Fuzzy controller service in C++17 (FuzzyLite + cpp-httplib), used in the experiments. |
| `controllers/fuzzy_py/` | Equivalent controller in Python (scikit-fuzzy + Flask). |
| `fuzzy-ctrl.yaml` | Kubernetes Deployment + NodePort Service of the C++ controller. |
| `collector.yaml` | `fastcollector` DaemonSet for node CPU/network metrics. |
| `scripts/config.env` | Environment used by the plants when run through `make`. |
| `docs/` | Detailed documentation (see below). |

### Documentation

| Document | Content |
|----------|---------|
| [docs/architecture.md](docs/architecture.md) | Components, control loop, threading and timing model, Kubernetes deployment. |
| [docs/plants.md](docs/plants.md) | Plant models, parameters, setpoint profiles, gains, runtime configuration. |
| [docs/controller.md](docs/controller.md) | Fuzzy inference system, HTTP API, concurrency, logging, configuration. |
| [docs/data_schema.md](docs/data_schema.md) | Column-by-column description of every CSV produced, and how to join them. |
| [docs/dependencies.md](docs/dependencies.md) | Libraries, versions and licenses. |

### Quick start (local)

Requirements: `gcc`, `make`, `libcurl` development headers, `cmake` ≥ 3.14, a C++17 compiler and internet access for the first CMake configure (dependencies are downloaded).

```bash
make all                       # plants + C++ controller

make run_ctrl                  # terminal 1: controller on :5000, log in ./Result_CSS_*.csv

# terminal 2: DC motor, piecewise setpoint, paced at 10 ms, 40 s
REQUEST_PERIOD_MS=10 DURATION_S=40 ./plants/bin/dc_motor 2 res_dc_motor_client1.csv
```

Each plant writes one CSV row per control request, and the controller writes one row per request it serves; both carry the same `(client id, seq)` pair, so every request can be matched end to end (see [docs/data_schema.md](docs/data_schema.md)).

### Kubernetes

```bash
make docker_cpp && docker push ramonvfs/fuzzy-ctrl-cpp:v3
kubectl apply -f fuzzy-ctrl.yaml -f collector.yaml
kubectl scale deployment fuzzy-ctrl-cpp --replicas=<K>
```

Controller logs are written on each node to `/tmp/dataset/fuzzy-ctrl/` (one file per pod). Plants point to the NodePort, e.g. `CONTROLLER_URL=http://<node-ip>:30005/control`.
