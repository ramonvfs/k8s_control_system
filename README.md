## K8S Control System

[Português](README.pt-BR.md)

Environment for evaluating fuzzy controllers deployed as horizontally scalable services on Kubernetes. Plants (car, DC motor, tank) are simulated in C and close their loop through HTTP requests to the controller, implemented in C++ (FuzzyLite) and Python (scikit-fuzzy).

```
 load generator (M plant processes)              Kubernetes cluster
 ┌──────────────────────────────┐         ┌──────────────────────────────────┐
 │ plant i                      │  HTTP   │ Service fuzzy-service (NodePort) │
 │  sim thread  (RK4, dt=10 ms) │  POST   │        │ kube-proxy              │
 │  ctrl thread ── e, ed ───────┼────────►│        ▼                         │
 │        ▲     ◄── s, pod ─────┼─────────│  K replicas of fuzzy-ctrl-cpp    │
 │        └─ v = sat(v + s)     │         │  (FIM + per-pod CSV log)         │
 │  CSV: one row per request    │         │ fastcollector: node CPU/network  │
 └──────────────────────────────┘         └──────────────────────────────────┘
```

| Path | Content |
|------|---------|
| `plants/` | Plant simulators (C). `common/` is the shared runtime; `car/`, `dc_motor/`, `tank/` define the models. |
| `controllers/fuzzy_cpp/` | Controller service, C++17. |
| `controllers/fuzzy_py/` | Equivalent controller, Python. |
| `fuzzy-ctrl.yaml`, `collector.yaml` | Kubernetes manifests (controller, metrics collector). |
| `scripts/config.env` | Plant environment used by `make run_*`. |

Documentation: [architecture](docs/architecture.md) · [plants](docs/plants.md) · [controller](docs/controller.md) · [data schema](docs/data_schema.md) · [dependencies](docs/dependencies.md)

### Quick start

Requires `gcc`, `make`, libcurl headers, `cmake` ≥ 3.14 and a C++17 compiler (CMake downloads the C++ dependencies).

```bash
make all                                    # plants + C++ controller
make run_ctrl                               # terminal 1: controller on :5000
REQUEST_PERIOD_MS=10 DURATION_S=40 ./plants/bin/dc_motor 2 res_dc_motor_client1.csv   # terminal 2
```

### Kubernetes

```bash
make docker_cpp && docker push ramonvfs/fuzzy-ctrl-cpp:v3
kubectl apply -f fuzzy-ctrl.yaml -f collector.yaml
kubectl scale deployment fuzzy-ctrl-cpp --replicas=<K>
```

Plants use `CONTROLLER_URL=http://<node-ip>:30005/control`. Controller logs are written on each node to `/tmp/dataset/fuzzy-ctrl/`. With many plants, tune the load generator first ([port exhaustion](docs/architecture.md#ephemeral-port-exhaustion)).
