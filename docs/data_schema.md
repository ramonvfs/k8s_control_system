# Data schema

[Português](pt-BR/data_schema.md)

CSV files with one header line. `TimestampFormat_us` (`YYYY-MM-DD HH:MM:SS.ffffff`) and `Timestamp_us` (µs since epoch) are the same instant, in UTC.

## Plant log

One row per request (or every `LOG_EVERY`).

| Column | Unit | Description |
|--------|------|-------------|
| `TimestampFormat_us`, `Timestamp_us` | – | Time the request was sent. |
| `t` | s | Simulation time of the state used. |
| `seq` | – | Request index from 0, sent to the controller. |
| `setpoint`, `y`, `error` | plant¹ | Reference, output, setpoint − y. |
| `e`, `ed` | – | Normalized inputs sent: `ki·error`, `kp·Δerror`. |
| `s` | – | Controller output (0 on failure). |
| `v` | input¹ | Control input applied. |
| `requestDelay` | ms | Round-trip time of the HTTP transaction. |
| `connectTime` | ms | Until TCP connection established (0 if reused). |
| `firstByteTime` | ms | Until first response byte (includes `connectTime`). |
| `measuredLoopTime` | ms | Time since the previous iteration started. |
| `requestTime` | ms | Configured `REQUEST_PERIOD_MS` (0 = free-running). |
| `httpStatus`, `curlCode` | – | HTTP status (0 = no reply) and libcurl code (0 = OK). |
| `ok` | 0/1 | Request succeeded and `u` was parsed. |
| `pod` | – | Replica that answered. |

¹ car: m/s, N · dc_motor: rad/s, V · tank: m, 0–1.

## Controller log

| Column | Unit | Description |
|--------|------|-------------|
| `TimestampFormat_us`, `Timestamp_us` | – | Time the request reached the handler. |
| `ElapsedTime_us` | µs | Since process start. |
| `Pod` | – | Replica name. |
| `IIoT_name`, `seq` | – | `client_id` and `seq` sent by the plant (`-`, −1 if absent). |
| `e`, `ed`, `s` | – | Inputs as received (before clamping) and output. |
| `Request_rate` | req/s | Requests on this pod in the last `RATE_WINDOW_MS`. |
| `LockWait_us` | µs | Wait for the inference mutex. |
| `Inference_us` | µs | FIS computation. |
| `Handler_us` | µs | Handler entry to response ready. |

## Joining

```python
joined = plant.assign(IIoT_name=client_id).merge(ctrl, on=["IIoT_name", "seq"])
joined["network_ms"] = joined.requestDelay - joined.Handler_us / 1000
```

`seq` restarts in each plant process, so `client_id` must be unique per run.
