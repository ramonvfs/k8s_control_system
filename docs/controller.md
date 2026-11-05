# Fuzzy controller service

Two implementations of the same controller are provided. The C++ one (`controllers/fuzzy_cpp`) is the one deployed in the experiments; the Python one (`controllers/fuzzy_py`) is a reference implementation with the same API and log format.

| File | Role |
|------|------|
| `fuzzy_cpp/src/fuzzy_fim.hpp` | Fuzzy Inference Machine (FuzzyLite engine). |
| `fuzzy_cpp/src/csv_logger.hpp` | Asynchronous CSV writer. |
| `fuzzy_cpp/src/main.cpp` | HTTP server, configuration, request accounting, shutdown. |
| `fuzzy_py/control.py` | Python implementation (scikit-fuzzy + Flask). |

## Fuzzy Inference Machine

Mamdani system with two inputs and one output, all on the normalized universe [−1, 1].

| Property | Value |
|----------|-------|
| Inputs | `error` (e), `delta_error` (ed); clamped to [−1, 1] |
| Output | `output` (s), increment of the control input |
| Terms per variable | 7: LN, MN, SN, ZZ, SP, MP, LP |
| Rules | 49 (complete 7×7 table) |
| Conjunction (AND) | Minimum |
| Implication | Minimum |
| Aggregation | Maximum |
| Activation | General (all rules fire; FuzzyLite default) |
| Defuzzification | Centroid, 100 samples (FuzzyLite) / 101-point universe (scikit-fuzzy) |
| Default output | 0 (no rule activated) |

Membership functions (identical for the three variables):

| Term | Shape | Parameters |
|------|-------|------------|
| LN (large negative) | Trapezoid | −1.0, −1.0, −0.7, −0.4 |
| MN (medium negative) | Triangle | −0.7, −0.4, −0.1 |
| SN (small negative) | Triangle | −0.4, −0.1, 0.0 |
| ZZ (zero) | Triangle | −0.1, 0.0, 0.1 |
| SP (small positive) | Triangle | 0.0, 0.1, 0.4 |
| MP (medium positive) | Triangle | 0.1, 0.4, 0.7 |
| LP (large positive) | Trapezoid | 0.4, 0.7, 1.0, 1.0 |

Rule table (`if error is <row> and delta_error is <column> then output is <cell>`):

| e \ ed | LN | MN | SN | ZZ | SP | MP | LP |
|--------|----|----|----|----|----|----|----|
| **LN** | LN | LN | LN | LN | MN | SN | ZZ |
| **MN** | LN | LN | LN | MN | SN | ZZ | SP |
| **SN** | LN | LN | MN | SN | ZZ | SP | MP |
| **ZZ** | LN | MN | SN | ZZ | SP | MP | LP |
| **SP** | MN | SN | ZZ | SP | MP | LP | LP |
| **MP** | SN | ZZ | SP | MP | LP | LP | LP |
| **LP** | ZZ | SP | MP | LP | LP | LP | LP |

The C++ and Python outputs differ only by the defuzzification discretization: over 205 input pairs (5 hand-picked + 200 uniformly random in [−1, 1]²) the maximum absolute difference was 6.4·10⁻⁴ and the mean 1.2·10⁻⁴.

## HTTP API

### `POST /control`

Request (JSON):

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `error` | number | no (0) | Normalized error e. |
| `delta_error` | number | no (0) | Normalized error variation ed. |
| `client_id` | string | no | Plant identifier, logged as `IIoT_name`. |
| `seq` | integer | no (−1) | Request index of the plant, logged as `seq`. |

Response `200` (JSON): `{"u": <FIS output>, "pod": "<pod name>"}`. `u` is always the first key, so plant binaries that parse the reply with `sscanf("{\"u\": %lf}")` keep working. Invalid JSON or non-numeric inputs → `400` with `{"error": "Invalid JSON or Processing Error"}` (not logged).

### `GET /health`

`{"status": "ok", "pod": "<pod name>"}`. Used as the Kubernetes readiness probe.

## Concurrency model

- **C++**: cpp-httplib serves connections from a thread pool (default `max(8, hardware_concurrency − 1)` threads, growing up to 4× that on demand; override with `HTTP_THREADS`). Inside a container `hardware_concurrency` reports the CPUs of the node, not the CPU limit of the pod. The FuzzyLite engine is not thread-safe, so inference is serialized by one mutex per process: **a pod processes one inference at a time**, and throughput scales only by adding replicas. JSON parsing and response building happen outside the mutex. Other cpp-httplib defaults that are relevant under load: listen backlog 5 (`CPPHTTPLIB_LISTEN_BACKLOG`), keep-alive timeout 5 s and at most 100 requests per connection.
- **Python**: Flask's built-in (Werkzeug) server, one thread per request; the scikit-fuzzy `ControlSystemSimulation` is serialized with a `threading.Lock`. This server is meant for development and the scikit-fuzzy inference takes tens of milliseconds, so this implementation is not suited to 10 ms loops.

## Per-request log

Each replica writes one CSV file with one row per `/control` request (schema in [data_schema.md](data_schema.md#controller-log-result_css_csv)). Timing points inside the handler:

```
handler entry ─── parse JSON ─── wait for FIM mutex ─── inference ─── (response, log line)
     │                │                  │                  │
  t_entry         t_parsed           t_locked            t_done
                      └─ LockWait_us ───┘└─ Inference_us ──┘
  └──────────────────────────── Handler_us ─────────────────┘
```

`Request_rate` is the number of requests that acquired the mutex on this pod during the last `RATE_WINDOW_MS` (sliding window, including the current one), divided by the window length: requests per second per pod.

The handler only appends a preformatted line to an in-memory buffer; a background thread writes the buffer to disk once per second. On SIGINT/SIGTERM the server stops accepting connections, finishes in-flight requests and writes the remaining lines before exiting.

## Configuration (environment variables)

| Variable | Default | Description |
|----------|---------|-------------|
| `PORT` | `5000` | Listening port. |
| `POD_NAME` | `$HOSTNAME` / hostname | Replica name, written in the log and returned in the reply. |
| `NODE_NAME` | `-` | Node name (startup banner only). |
| `CONTROLLER_LOG` | `1` | `0` disables the CSV log. |
| `LOG_DIR` | `.` | Directory of the CSV log. |
| `EXP_NAME` | empty | Optional tag inserted in the file name. |
| `HTTP_THREADS` | cpp-httplib default | Fixed worker thread count (C++ only). |
| `RATE_WINDOW_MS` | `1000` | Window of `Request_rate`. |

Log file name: `Result_CSS_<YYYY-MM-DD_HH-MM-SS>[_Exp_<EXP_NAME>]_pod_<POD_NAME>.csv`, where the date is the process start time in UTC. A pod writes a single file for its whole lifetime; rows of different experiment rounds are told apart by `IIoT_name` (which, with the default plant `CLIENT_ID`, contains plant, M, K and client index) and by time.

## Building

```bash
make ctrl_cpp                   # native build in controllers/fuzzy_cpp/build
make docker_cpp CPP_TAG=v3      # container image (Alpine 3.19, multi-stage)
make setup_py                   # virtualenv for the Python controller
```
