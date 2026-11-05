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

### Ephemeral port exhaustion on the load generator

Opening one TCP connection per request has a cost on the host that runs the plants. The plant (libcurl) closes each connection right after the reply, so the plant side performs the *active close* and each socket stays in `TIME_WAIT` for 60 s (`TCP_TIMEWAIT_LEN`, fixed in the Linux kernel; `tcp_fin_timeout` does **not** shorten it). Every socket in `TIME_WAIT` keeps its local port reserved for the same destination (IP, port). With the default range `net.ipv4.ip_local_port_range = 32768 60999` (28 232 ports), the sustainable rate towards one destination is about

$$ \frac{28\,232 \text{ ports}}{60 \text{ s}} \approx 470 \text{ new connections/s} $$

for the whole load-generator host, i.e. summed over all *M* plants. Above that rate the ports run out after `28 232 / rate` seconds and new requests fail: the plant logs them with `ok = 0` and a non-zero `curlCode` (observed: 56, *failure when receiving data from the peer*; 7 or 99 are also possible), and holds its control input (`s = 0`). The failures last until the oldest `TIME_WAIT` sockets expire, so in runs longer than 60 s they come and go in bursts.

Observed in a local test (3 controller containers behind nginx, 4 free-running plants, ~670 req/s in total): the number of `TIME_WAIT` sockets grew linearly and stopped at 28 235; from t ≈ 23 s on, practically every request failed and the plants ran in open loop for the rest of the 60 s run. In the cluster the rate is `M / RTT` in free-running mode or `M × 1000 / REQUEST_PERIOD_MS` with a fixed period (e.g. M = 32 at 10 ms → 3 200 connections/s), so the limit is reached with few clients.

Versions of the plants that logged only `Timestamp(s),RTT(ms),Setpoint,Output,ControlInput` hid these failures: a failed request appeared as a normal row (only stderr showed `Network Error`). Check `ok`/`curlCode` before using a run.

**Fix: tune the load-generator host** (requires root; nothing changes on the cluster):

```bash
# Use (almost) the whole port range for outgoing connections: ~64 500 ports.
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# Allow a new outgoing connection to reuse a port in TIME_WAIT when TCP
# timestamps guarantee the old segments cannot be confused with the new
# connection. Default 2 = loopback only; 1 = all connections.
sudo sysctl -w net.ipv4.tcp_tw_reuse=1

# Required by tcp_tw_reuse (already 1 by default).
sudo sysctl -w net.ipv4.tcp_timestamps=1
```

To keep the settings after a reboot, write them to `/etc/sysctl.d/99-loadgen.conf` and run `sudo sysctl --system`. With `tcp_tw_reuse = 1` a port can be reused once its `TIME_WAIT` socket is older than `net.ipv4.tcp_tw_reuse_delay` (1 s by default on kernels that have it), which raises the limit far above the experiment rates. Do not look for `tcp_tw_recycle`: it was unsafe behind NAT and was removed in Linux 4.12.

Alternatives, each with a side effect on what is being measured:

| Option | Effect |
|--------|--------|
| `HTTP_KEEPALIVE=1` | One connection per plant, no `TIME_WAIT` build-up. But kube-proxy balances connections, so each plant stays on one replica and adding replicas no longer spreads a plant's load. |
| `REQUEST_PERIOD_MS` > 0 | Caps the rate at `M × 1000 / REQUEST_PERIOD_MS`; on its own enough only for small M. |
| Several source IPs on the load generator | Each (source IP, destination) pair has its own port range. |

Checking a host during a run:

```bash
ss -Htan state time-wait | wc -l          # sockets in TIME_WAIT (limit = size of the port range)
sysctl net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse
```

The cluster nodes keep NAT state for each Service connection in the conntrack table. At thousands of connections per second it is also worth checking `sysctl net.netfilter.nf_conntrack_count net.netfilter.nf_conntrack_max` on the nodes; a full table drops new connections.

### Source address seen by the controller

With `externalTrafficPolicy: Cluster` (the default) a request that arrives at one node can be forwarded to a pod on another node and is source-NATed; the `RemoteAddr` seen by the pod is then a node address, not the plant host. The plant identity is carried in the request body (`client_id`) for this reason.

## Clocks and time bases

| Quantity | Clock | Comparable across hosts? |
|----------|-------|--------------------------|
| `TimestampFormat_us` (UTC), `Timestamp_us` (plants and controller) | `CLOCK_REALTIME` / `system_clock` | Only if hosts are synchronized (NTP/chrony). Time zones do not matter: both columns are UTC. |
| RTT, loop time, service times, `ElapsedTime_us` | `CLOCK_MONOTONIC` / `steady_clock` | Durations only; immune to clock adjustments. |
| `t` (plants) | Simulation time, `k·dt` | Per plant, starts at 0. |

Durations should always be computed from the monotonic columns; wall-clock columns are meant for aligning series from different sources (plants, controller pods, fastcollector).
