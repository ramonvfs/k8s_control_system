# Architecture

## Components

| Component | Where it runs | Implementation |
|-----------|---------------|----------------|
| **Plants** (car, DC motor, tank) | Load-generator host, outside the cluster. Up to *M* processes run in parallel, one per simulated client. | C, `plants/` |
| **Fuzzy controller** (`fuzzy-ctrl-cpp`) | Kubernetes Deployment with *K* replicas, exposed by a NodePort Service (`:30005`). | C++17, `controllers/fuzzy_cpp/` |
| **Metrics collector** (`fastcollector`) | DaemonSet, one pod per node; samples CPU and network of the controller pods (label `app=fuzzy-ctrl`). | External image `ramonvfs/fastcollector:v1` |

The controller is stateless: a plant keeps its own loop state (previous error, integrated control input) and sends everything the controller needs in each request. Any replica can therefore serve any request, which is what makes horizontal scaling possible.

## Control loop across the network

Every plant process runs two POSIX threads that share the plant state through a mutex:

```
 simulation thread (period dt = 10 ms, absolute deadlines on CLOCK_MONOTONIC)
   u   <- shared.u
   x   <- RK4(x, u, dt)            t <- t + dt
   r   <- setpoint(exp_type, t)
   shared.{x, t, setpoint} <- x, t, r

 control thread (free-running or fixed period REQUEST_PERIOD_MS)
   y, r, t <- shared
   error   = r - y
   e       = ki * error
   ed      = kp * (error - error_prev)
   s       = POST /control {e, ed, client_id, seq}     <- network round trip
   v       = sat(v + s, u_min, u_max)
   shared.u <- v
   CSV row
```

The plant is integrated in real time independently of the network, so network delay manifests itself as it would in a physical plant: the input applied to the plant is held (zero-order hold) until the next reply arrives. If a request fails, `s = 0` and the last input is kept.

### Request pacing

- **Free-running** (`REQUEST_PERIOD_MS=0`, default, behaviour of the original experiments): the next request is sent as soon as the previous reply is received, so the sampling rate of the loop is 1/RTT.
- **Fixed period** (`REQUEST_PERIOD_MS=P`): requests start every *P* ms on absolute deadlines. If a request takes longer than *P* (overrun), the next one starts immediately and the schedule is re-anchored; missed slots are not caught up in bursts.

> **Note on free-running mode.** The FIS output is an *increment* of the control input, and the simulation state only changes every `dt`. With RTT < dt several requests observe the same `y`: the first one carries the whole error variation and the following ones have `ed = 0`, while each still adds an increment proportional to `e`. The effective integral gain per unit of time therefore grows with 1/RTT, i.e. the closed-loop dynamics depend on network latency and on cluster load. On a local machine (RTT ≈ 0.15 ms, ~66 requests per 10 ms step) the DC motor loop diverges both with the original and with the refactored code. When control performance must be comparable across (M, K) configurations, use `REQUEST_PERIOD_MS=10` (= dt); the free-running mode is useful as a throughput/stress test.

## Kubernetes deployment

`fuzzy-ctrl.yaml` defines:

- **Deployment `fuzzy-ctrl-cpp`**: CPU request 100m, limit 200m per pod. The pod name and node name are injected through the Downward API (`POD_NAME`, `NODE_NAME`) and used in the controller log. A readiness probe on `GET /health` keeps new replicas out of the Service until they can answer. On scale-down the pod receives SIGTERM, stops accepting requests and flushes its log within `terminationGracePeriodSeconds` (30 s).
- **Service `fuzzy-service`** (NodePort 30005 → container port 5000). kube-proxy load-balances **connections**, not HTTP requests. Because the plants open a new TCP connection per request by default (`HTTP_KEEPALIVE=0`), successive requests from one plant are spread among the replicas. With keep-alive enabled, a plant would stay on the replica chosen for its first connection.
- **Log volume**: `hostPath` `/tmp/dataset/fuzzy-ctrl` on the node, mounted at `/var/log/fuzzy-ctrl`. Each pod writes its own `Result_CSS_*_pod_<pod>.csv`, so files of different replicas never collide. The `fastcollector` DaemonSet writes to `/tmp/dataset` on the same node.

With `externalTrafficPolicy: Cluster` (the default) a request that arrives at one node can be forwarded to a pod on another node and is source-NATed; the `RemoteAddr` seen by the pod is then a node address, not the plant host. The plant identity is carried in the request body (`client_id`) for this reason.

## Clocks and time bases

| Quantity | Clock | Comparable across hosts? |
|----------|-------|--------------------------|
| `TimestampFormat_us`, `Timestamp_us` (plants and controller) | `CLOCK_REALTIME` / `system_clock` | Only if hosts are synchronized (NTP/chrony). |
| RTT, loop time, service times, `ElapsedTime_us` | `CLOCK_MONOTONIC` / `steady_clock` | Durations only; immune to clock adjustments. |
| `t` (plants) | Simulation time, `k·dt` | Per plant, starts at 0. |

Durations should always be computed from the monotonic columns; wall-clock columns are meant for aligning series from different sources (plants, controller pods, fastcollector).
