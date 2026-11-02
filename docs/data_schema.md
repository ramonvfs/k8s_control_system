# Data schema

Three kinds of files are produced in a run:

| File | Producer | Granularity |
|------|----------|-------------|
| `res_<plant>_M<m>_K<k>_EXP<n>_client<i>.csv` (name chosen by the runner) | Each plant process | One row per control request (or every `LOG_EVERY`). |
| `Result_CSS_<start>[_Exp_<name>]_pod_<pod>.csv` | Each controller replica | One row per request served. |
| `metrics*.csv` | `fastcollector` DaemonSet, per node | Periodic CPU / network samples (external tool). |

All files are comma-separated with a single header line. Wall-clock timestamps are in local time (`TimestampFormat_us`) and in microseconds since the Unix epoch (`Timestamp_us`).

## Plant log (`res_*.csv`)

| Column | Unit | Description |
|--------|------|-------------|
| `TimestampFormat_us` | `YYYY-MM-DD HH:MM:SS.ffffff` | Wall-clock time (local) at which the request was issued. |
| `Timestamp_us` | µs since epoch | Same instant, UTC epoch. |
| `t` | s | Simulation time of the state used in this request (multiple of `dt`). |
| `seq` | – | Request index, starting at 0. Sent to the controller. |
| `setpoint` | plant unit¹ | Reference r. |
| `y` | plant unit¹ | Measured output (velocity, angular velocity or level). |
| `error` | plant unit¹ | r − y. |
| `e` | – | Normalized error sent to the controller, `ki·error`. |
| `ed` | – | Normalized error variation sent, `kp·(error − error_prev)`. |
| `s` | – | Controller output (increment). 0 when the request failed. |
| `v` | input unit¹ | Control input applied to the plant after the update, `sat(v_prev + s)`. |
| `requestDelay` | ms | Round-trip time of the request, measured with `CLOCK_MONOTONIC` around the whole HTTP transaction (TCP connect, request, server processing, response). |
| `connectTime` | ms | libcurl `CONNECT_TIME`: from the start of the transaction until the TCP connection was established (0 when a connection was reused). |
| `firstByteTime` | ms | libcurl `STARTTRANSFER_TIME`: from the start until the first byte of the response (includes `connectTime`). |
| `measuredLoopTime` | ms | Interval between the start of this iteration and the previous one (0 in the first row). |
| `requestTime` | ms | Configured request period (`REQUEST_PERIOD_MS`); 0 = free-running. |
| `httpStatus` | – | HTTP status code; 0 if no response was received. |
| `curlCode` | – | libcurl `CURLcode` (0 = OK, 7 = connection refused, 28 = timeout, …). |
| `ok` | 0/1 | 1 if the transfer succeeded, status was 200 and `u` was parsed. |
| `pod` | – | Controller replica that served the request (from the reply). Empty when `ok = 0`. |

¹ Units per plant:

| Plant | `setpoint`, `y`, `error` | `v` |
|-------|--------------------------|-----|
| car | m/s | N (traction force) |
| dc_motor | rad/s | V (armature voltage) |
| tank | m | – (pump command, 0–1) |

Derived quantities:

- Network + server-stack time of a request: `requestDelay − Handler_us/1000` (join with the controller log, below).
- Requests per simulation step: `dt / measuredLoopTime` (see the note on free-running mode in [architecture.md](architecture.md#request-pacing)).
- Failure rate: `1 − mean(ok)`.

## Controller log (`Result_CSS_*.csv`)

| Column | Unit | Description |
|--------|------|-------------|
| `TimestampFormat_us` | `YYYY-MM-DD HH:MM:SS.ffffff` | Wall-clock time (local) when the request reached the handler. |
| `Timestamp_us` | µs since epoch | Same instant, UTC epoch. |
| `ElapsedTime_us` | µs | Time since the controller process started (monotonic). |
| `Pod` | – | Replica name (`POD_NAME`). |
| `IIoT_name` | – | `client_id` sent by the plant (`-` if absent). |
| `RemoteAddr` | – | Peer IP address seen by the pod (often a node address, because of SNAT in the Service). |
| `seq` | – | `seq` sent by the plant (−1 if absent). |
| `e`, `ed` | – | Inputs as received (before clamping to [−1, 1]). |
| `s` | – | FIS output returned. |
| `Request_rate` | req/s | Requests served by this pod in the last `RATE_WINDOW_MS` (default 1 s), including this one. |
| `LockWait_us` | µs | Time waiting for the inference mutex, i.e. queueing behind other requests on the same pod. |
| `Inference_us` | µs | FIS computation time. |
| `Handler_us` | µs | Handler entry → response ready (JSON parse + wait + inference). Excludes TCP accept, thread-pool queueing, HTTP parsing by the server library and the socket write. |

## Joining plant and controller rows

Every request is identified by `(client id, seq)`:

```python
import glob, pandas as pd

ctrl = pd.concat(pd.read_csv(f) for f in glob.glob("**/Result_CSS_*.csv", recursive=True))
plant = pd.read_csv("res_dc_motor_M4_K2_EXP2_client1.csv")
plant["IIoT_name"] = "res_dc_motor_M4_K2_EXP2_client1"   # = CLIENT_ID used in the run

joined = plant.merge(ctrl, on=["IIoT_name", "seq"], suffixes=("_plant", "_ctrl"))
joined["network_ms"] = joined["requestDelay"] - joined["Handler_us"] / 1000
```

Plant rows with `ok = 0` have no controller counterpart when the request never reached the handler. Note that `seq` restarts at 0 for every plant process, so a `client_id` must be unique per run (the default, derived from the CSV name, is).

## Correspondence with the reference dataset

Mapping between the columns of the reference experiment (`monitors_var_iiot-*.csv` and `Result_CSS_*.csv`) and the files produced here:

| Reference column | This project |
|------------------|--------------|
| monitors_var: `TimestampFormat_us` | `TimestampFormat_us` (+ `Timestamp_us`) |
| monitors_var: `t` | `t` |
| monitors_var: `e`, `ed` | `e`, `ed` |
| monitors_var: `setpoint`, `y`, `v` | `setpoint`, `y`, `v` (SI units, e.g. rad/s instead of RPM) |
| monitors_var: `requestDelay` | `requestDelay` |
| monitors_var: `measuredLoopTime` | `measuredLoopTime` |
| monitors_var: `requestTime` | `requestTime` |
| Result_CSS: `TimestampForm_us` | `TimestampFormat_us` |
| Result_CSS: `ElapsedTime_us` | `ElapsedTime_us` |
| Result_CSS: `IIoT_name` | `IIoT_name` |
| Result_CSS: `e`, `ed`, `s` | `e`, `ed`, `s` |
| Result_CSS: `Request_rate` | `Request_rate` |

Additional columns here: `seq`, `error`, `s`, `connectTime`, `firstByteTime`, `httpStatus`, `curlCode`, `ok`, `pod` (plant); `Timestamp_us`, `Pod`, `RemoteAddr`, `seq`, `LockWait_us`, `Inference_us`, `Handler_us` (controller). The reference also exposed the plant variables through a Prometheus endpoint; that is not implemented here.

## Correspondence with the previous plant log format

Earlier versions wrote `Timestamp(s),RTT(ms),Setpoint(<unit>),<Velocity|Level>(<unit>),ControlInput(<unit>)`:

| Previous column | New column |
|-----------------|------------|
| `Timestamp(s)` (time since the control thread started) | `t` (simulation time) or `Timestamp_us` |
| `RTT(ms)` | `requestDelay` |
| `Setpoint(...)` | `setpoint` |
| `Velocity(...)` / `Level(m)` | `y` |
| `ControlInput(...)` | `v` |
