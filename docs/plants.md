# Plant simulators

Three benchmark plants are simulated in C. All of them share the runtime in `plants/common/`; each `plants/<name>/main.c` only provides a `plant_model_t` (see `plants/common/include/plant.h`) with the dynamics, setpoint profile, gains and saturation limits.

| File | Role |
|------|------|
| `common/include/plant.h`, `common/src/plant_runtime.c` | Model interface; simulation and control threads, configuration, signal handling, CSV log. |
| `common/include/network.h`, `common/src/network.c` | libcurl client for the controller (`POST /control`). |
| `common/include/solver.h`, `common/src/solver.c` | Fixed-step 4th-order Runge–Kutta integrator. |

## Numerical integration

Each plant is integrated with classical RK4, step `dt = 0.01 s`, with the control input held constant during the step (zero-order hold). The simulation thread runs in real time: each step is scheduled with `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` on absolute deadlines, so the period does not drift with the computation time. All states start at zero.

## Control law

The controller is an incremental (velocity-form) fuzzy PI. Per control iteration *k*:

$$
\begin{aligned}
\text{error}_k &= r_k - y_k \\
e_k &= k_i \cdot \text{error}_k \\
ed_k &= k_p \cdot (\text{error}_k - \text{error}_{k-1}) \\
s_k &= \mathrm{FIS}(e_k, ed_k) \in [-1, 1] \\
v_k &= \mathrm{sat}(v_{k-1} + s_k,\; u_{min},\; u_{max})
\end{aligned}
$$

Because the FIS output is an increment, the term in `e` plays the role of integral action (`ki`) and the term in the error variation plays the role of proportional action (`kp`). The controller clamps `e` and `ed` to [−1, 1] before inference. `error_{-1} = 0`, so the first request carries `ed = kp·error_0`.

## Models

### Car (`plants/car`)

First-order longitudinal model:

$$ m\,\dot v = F - b\,v $$

| Symbol | Value | Description |
|--------|-------|-------------|
| m | 450 kg | Vehicle mass |
| b | 20 N·s/m | Damping coefficient |
| y = v | m/s | Controlled output (velocity) |
| u = F | [−4000, 8000] N | Traction/braking force |
| ki, kp | 1/180, 5 | Error and error-variation gains |

### DC motor (`plants/dc_motor`)

Armature-controlled motor with constant load torque:

$$
\begin{aligned}
J\,\dot\omega &= K\,i - b\,\omega - \tau_c \\
L\,\dot i &= V - K\,\omega - R\,i
\end{aligned}
$$

| Symbol | Value | Description |
|--------|-------|-------------|
| R | 0.2 Ω | Armature resistance |
| L | 0.1 H | Armature inductance |
| J | 0.05 kg·m² | Rotor inertia |
| b | 0.001 N·m·s | Viscous friction |
| K | 0.5 N·m/A (V·s/rad) | Torque / back-EMF constant |
| τc | 0.2 N·m | Load torque |
| y = ω | rad/s | Controlled output (angular velocity) |
| u = V | [−36, 36] V | Armature voltage |
| ki, kp | 1/2000, 0.5 | Error and error-variation gains |

> The control input enters the electrical equation as a voltage. Earlier logs labelled it `ControlInput(A)`; the unit is volts.

### Tank (`plants/tank`)

Single tank with pump inflow and Torricelli outflow:

$$ A\,\dot h = q_m\,u - V_o\,a\,\sqrt{2 g h} $$

| Symbol | Value | Description |
|--------|-------|-------------|
| A | 1.0 m² | Tank cross-section |
| a | 0.1 m² | Outlet pipe cross-section |
| q_m | 2.0 m³/s | Maximum pump flow |
| g | 9.8 m/s² | Gravity |
| V_o | 0.5 | Outlet valve opening (fixed) |
| H_max | 30 m | Maximum level: at H_max a positive net inflow is discarded (ḣ = 0) |
| y = h | m | Controlled output (level) |
| u | [0, 1] | Pump command (inflow = q_m·u) |
| ki, kp | 1/30, 100 | Error and error-variation gains |

Negative levels caused by numerical error are treated as 0 inside the square root.

## Setpoint profiles

Selected by the first command-line argument (`exp_type`). Type 1 is a constant reference; any other value selects the piecewise profile below (`t` is simulation time).

| Plant | Type 1 | Type 2: 0 < t ≤ T1 | T1 < t ≤ T2 | T2 < t ≤ T3 | t > T3 | (T1, T2, T3) |
|-------|--------|-------------------|-------------|-------------|--------|--------------|
| car (m/s) | 20.0 | 20.0 (72 km/h) | 41.67 (150 km/h) | 13.89 (50 km/h) | 50.0 (180 km/h) | (50, 100, 150) s |
| dc_motor (rad/s) | 52.36 | 52.36 (500 RPM) | 31.42 (300 RPM) | 62.83 (600 RPM) | 41.89 (400 RPM) | (10, 20, 30) s |
| tank (m) | 20.0 | 20.0 | 10.0 | 25.0 | 15.0 | (50, 100, 150) s |

## Running a plant

```
plants/bin/<car|dc_motor|tank> [exp_type] [csv_file]
```

| Argument | Default | Description |
|----------|---------|-------------|
| `exp_type` | `1` | Setpoint profile (see above). |
| `csv_file` | `<plant>_network_log.csv` | Output CSV (schema in [data_schema.md](data_schema.md)). |

Environment variables (defaults in parentheses; `scripts/config.env` sets them for `make run_*`):

| Variable | Description |
|----------|-------------|
| `CONTROLLER_URL` (`http://localhost:5000/control`) | Controller endpoint. |
| `CLIENT_ID` (CSV file name without `.csv`) | Identifier sent with every request and logged by the controller as `IIoT_name`. The default makes the run, M, K and client index visible in the controller log when files follow the `res_<plant>_M<m>_K<k>_EXP<n>_client<i>.csv` convention. |
| `REQUEST_PERIOD_MS` (`0`) | 0 = free-running; > 0 = fixed request period (see [architecture.md](architecture.md#request-pacing)). |
| `DURATION_S` (`0`) | Stop after this many seconds of control loop; 0 = until SIGINT/SIGTERM. |
| `HTTP_KEEPALIVE` (`0`) | 0 = new TCP connection per request; 1 = reuse the connection. |
| `HTTP_TIMEOUT_MS` (`0`) | Request timeout; 0 = none. A timed-out request counts as failed (`s = 0`). |
| `LOG_EVERY` (`1`) | Write one CSV row every N iterations. |

At startup the plant prints its full configuration (model, gains, limits, network settings) to stdout; redirecting stdout to a `.log` file next to the CSV keeps the metadata of each run.

### Termination

The plant stops when `DURATION_S` elapses or on SIGINT/SIGTERM (e.g. `timeout 215 plants/bin/car 2 ...`). The CSV is block-buffered and flushed once per second and at exit; with SIGKILL at most the last second of data is lost. A request in progress is completed before exiting.
