# Plant simulators

[Português](pt-BR/plants.md)

Each `plants/<name>/main.c` defines a `plant_model_t` (`common/include/plant.h`): dynamics, setpoint profile, gains and saturation. `common/src/plant_runtime.c` provides the threads, configuration, signals and CSV log; `network.c` the libcurl client; `solver.c` a fixed-step RK4 (`dt = 0.01 s`, input held during the step). States start at zero.

## Control law

Incremental fuzzy PI, per iteration *k*:

$$
e_k = k_i\,\text{error}_k,\quad ed_k = k_p(\text{error}_k - \text{error}_{k-1}),\quad v_k = \mathrm{sat}\big(v_{k-1} + \mathrm{FIS}(e_k, ed_k)\big)
$$

with `error = setpoint − y`. Since the FIS output is an increment, `ki` acts as integral gain and `kp` as proportional gain.

## Models

**Car**: $m\dot v = F - b v$

| m | b | y | u | ki, kp |
|---|---|---|---|--------|
| 450 kg | 20 N·s/m | v (m/s) | F ∈ [−4000, 8000] N | 1/180, 5 |

**DC motor**: $J\dot\omega = K i - b\omega - \tau_c$, $\;L\dot i = V - K\omega - R i$

| R | L | J | b | K | τc | y | u | ki, kp |
|---|---|---|---|---|----|---|---|--------|
| 0.2 Ω | 0.1 H | 0.05 kg·m² | 0.001 N·m·s | 0.5 N·m/A | 0.2 N·m | ω (rad/s) | V ∈ [−36, 36] V | 1/2000, 0.5 |

**Tank**: $A\dot h = q_m u - V_o a \sqrt{2 g h}$, with ḣ = 0 at H_max for positive net inflow.

| A | a | q_m | g | V_o | H_max | y | u | ki, kp |
|---|---|-----|---|-----|-------|---|---|--------|
| 1 m² | 0.1 m² | 2 m³/s | 9.8 m/s² | 0.5 | 30 m | h (m) | pump command ∈ [0, 1] | 1/30, 100 |

## Setpoint profiles

`exp_type = 1`: constant (first value below). Otherwise piecewise, switching at T1, T2, T3:

| Plant | Values | T1, T2, T3 |
|-------|--------|------------|
| car (m/s) | 20, 41.67, 13.89, 50 | 50, 100, 150 s |
| dc_motor (rad/s) | 52.36, 31.42, 62.83, 41.89 | 10, 20, 30 s |
| tank (m) | 20, 10, 25, 15 | 50, 100, 150 s |

## Usage

```
plants/bin/<car|dc_motor|tank> [exp_type=1] [csv_file=<plant>_network_log.csv]
```

| Variable (default) | Description |
|--------------------|-------------|
| `CONTROLLER_URL` (`http://localhost:5000/control`) | Controller endpoint. |
| `CLIENT_ID` (CSV name without `.csv`) | Sent with each request; logged by the controller as `IIoT_name`. |
| `REQUEST_PERIOD_MS` (`0`) | 0 = free-running; > 0 = fixed period. |
| `DURATION_S` (`0`) | Elapsed time after which the plant stops; 0 = until SIGINT/SIGTERM. |
| `HTTP_KEEPALIVE` (`0`) | 1 = reuse the TCP connection. |
| `HTTP_TIMEOUT_MS` (`0`) | Request timeout; 0 = none. |
| `LOG_EVERY` (`1`) | Log one row every N iterations. |

The configuration is printed to stdout at startup. `DURATION_S` is checked after each request. The CSV is flushed every second and on exit.
