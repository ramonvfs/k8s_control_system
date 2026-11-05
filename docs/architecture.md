# Architecture

[Português](pt-BR/architecture.md)

## Components

| Component | Runs on | Code |
|-----------|---------|------|
| Plants | Load-generator host; *M* processes, one per client | `plants/` |
| Fuzzy controller | Kubernetes Deployment, *K* replicas, NodePort Service `:30005` | `controllers/fuzzy_cpp/` |
| Metrics collector | DaemonSet, CPU/network of pods labelled `app=fuzzy-ctrl` | image `ramonvfs/fastcollector:v1` |

The controller is stateless: each request carries everything needed (the plant keeps the previous error and the integrated control input), so any replica can serve any request.

## Control loop

Each plant runs two threads sharing the state through a mutex:

```
 simulation thread (dt = 10 ms)            control thread
   u <- shared.u                             y, r <- shared
   x <- RK4(x, u, dt); t += dt               e  = ki·(r − y)
   r <- setpoint(t)                          ed = kp·((r − y) − error_prev)
   shared <- x, t, r                         s  = POST /control {e, ed, client_id, seq}
   sleep until next deadline                 v  = sat(v + s); shared.u <- v; CSV row
```

The plant is integrated in real time, independently of the network: the input is held until the next reply arrives, and a failed request gives `s = 0`. The simulation thread sleeps until absolute deadlines (`clock_nanosleep`, `TIMER_ABSTIME`), so computation time does not accumulate as drift; late steps run back to back until the simulation catches up.

### Request pacing

- **Free-running** (`REQUEST_PERIOD_MS=0`, default): the next request is sent as soon as the reply arrives; one request in flight per plant.
- **Fixed period** (`REQUEST_PERIOD_MS=P`): requests start every *P* ms; after an overrun the schedule is re-anchored, without bursts.

In free-running mode the loop rate is 1/RTT. Since the FIS output is an increment and `y` only changes every `dt`, the effective integral gain grows with 1/RTT when RTT < dt: closed-loop behaviour then depends on network latency and load. Use `REQUEST_PERIOD_MS=10` (= dt) when control performance must be comparable across configurations.

## Kubernetes

- **Deployment**: CPU request 100m, limit 200m. `POD_NAME` and `NODE_NAME` come from the Downward API. Readiness probe `GET /health`. On SIGTERM the pod stops accepting requests and flushes its log (`terminationGracePeriodSeconds: 30`).
- **Service**: kube-proxy balances **connections**. Plants open one connection per request by default (`HTTP_KEEPALIVE=0`), so their requests spread across replicas; with keep-alive a plant stays on one replica. Because of SNAT the source IP is not meaningful; plants identify themselves with `client_id`.
- **Logs**: `hostPath` `/tmp/dataset/fuzzy-ctrl`, one file per pod.

### Ephemeral port exhaustion

The plant closes each connection after the reply, so the socket stays in `TIME_WAIT` for 60 s on the load generator (fixed by the kernel; `tcp_fin_timeout` does not change it). With the default port range (`32768–60999`, 28 232 ports) the host sustains about 28 232 / 60 ≈ **470 new connections/s** to one destination, summed over all plants. The request rate is `M / RTT` (free-running) or `M × 1000 / REQUEST_PERIOD_MS`. Above the limit new requests fail (`ok = 0`, `curlCode` ≠ 0) until old sockets expire.

Fix on the load generator:

```bash
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"
sudo sysctl -w net.ipv4.tcp_tw_reuse=1      # reuse TIME_WAIT ports for outgoing connections (needs tcp_timestamps=1, the default)
```

Persist in `/etc/sysctl.d/99-loadgen.conf`. Monitor with `ss -Htan state time-wait | wc -l`. Alternatives: `HTTP_KEEPALIVE=1` (but load is no longer spread per request) or several source IPs. At high rates also check `nf_conntrack_count` / `nf_conntrack_max` on the nodes.

## Clocks

| Columns | Clock | Note |
|---------|-------|------|
| `TimestampFormat_us`, `Timestamp_us` | realtime, UTC | Comparable across hosts only with NTP. |
| RTT, loop and service times, `ElapsedTime_us` | monotonic | Durations; immune to clock changes. |
| `t` | simulation time | Per plant, starts at 0. |
