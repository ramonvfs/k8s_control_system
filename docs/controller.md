# Fuzzy controller

[Português](pt-BR/controller.md)

`controllers/fuzzy_cpp` (C++: `fuzzy_fim.hpp` inference, `csv_logger.hpp` async log, `main.cpp` HTTP server) and `controllers/fuzzy_py` (Python) implement the same system, API and log format. Their outputs differ by less than 10⁻³ (defuzzification discretization).

## Inference system

Mamdani, inputs `error` and `delta_error` clamped to [−1, 1], output in [−1, 1]. AND = min, implication = min, aggregation = max, centroid defuzzification (100 samples), output 0 when no rule fires.

Membership functions (same for all variables):

| LN | MN | SN | ZZ | SP | MP | LP |
|----|----|----|----|----|----|----|
| trap(−1, −1, −0.7, −0.4) | tri(−0.7, −0.4, −0.1) | tri(−0.4, −0.1, 0) | tri(−0.1, 0, 0.1) | tri(0, 0.1, 0.4) | tri(0.1, 0.4, 0.7) | trap(0.4, 0.7, 1, 1) |

Rules (row: error, column: delta_error):

| | LN | MN | SN | ZZ | SP | MP | LP |
|-|----|----|----|----|----|----|----|
| **LN** | LN | LN | LN | LN | MN | SN | ZZ |
| **MN** | LN | LN | LN | MN | SN | ZZ | SP |
| **SN** | LN | LN | MN | SN | ZZ | SP | MP |
| **ZZ** | LN | MN | SN | ZZ | SP | MP | LP |
| **SP** | MN | SN | ZZ | SP | MP | LP | LP |
| **MP** | SN | ZZ | SP | MP | LP | LP | LP |
| **LP** | ZZ | SP | MP | LP | LP | LP | LP |

## API

- `POST /control` `{"error", "delta_error", "client_id", "seq"}` → `{"u", "pod"}` (`u` first, for older plants). Invalid input → 400.
- `GET /health` → `{"status": "ok", "pod"}`.

## Concurrency

cpp-httplib serves requests from a thread pool (default `max(8, nCPU − 1)`, where nCPU is the node's CPU count even inside a container; `HTTP_THREADS` overrides it). The FuzzyLite engine stores inputs and intermediate results in its own objects and is not thread-safe, so inference is serialized by a mutex: **one inference at a time per pod**; throughput scales with replicas. Other relevant cpp-httplib defaults: listen backlog 5, keep-alive timeout 5 s. The Python version (Flask development server, tens of ms per inference) is a reference, not suited to 10 ms loops.

## Log

One CSV per pod, one row per request ([schema](data_schema.md#controller-log)). Timing inside the handler:

```
entry ── parse ── wait mutex ── inference ── response
        t_parsed  └LockWait_us┘└Inference_us┘
└─────────────────── Handler_us ───────────┘
```

Rows are buffered in memory and written by a background thread every second and on SIGTERM. File name: `Result_CSS_<start, UTC>[_Exp_<EXP_NAME>]_pod_<POD_NAME>.csv`.

| Variable (default) | Description |
|--------------------|-------------|
| `PORT` (`5000`) | Listening port. |
| `POD_NAME` (hostname) | Replica name in log and reply. |
| `NODE_NAME` (`-`) | Shown at startup. |
| `CONTROLLER_LOG` (`1`) | 0 disables the log. |
| `LOG_DIR` (`.`) | Log directory. |
| `EXP_NAME` (empty) | Optional tag in the file name. |
| `HTTP_THREADS` (library default) | Worker threads (C++). |
| `RATE_WINDOW_MS` (`1000`) | Window of `Request_rate`. |
