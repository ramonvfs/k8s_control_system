# Simuladores das plantas

[English](../plants.md)

Cada `plants/<nome>/main.c` define um `plant_model_t` (`common/include/plant.h`): dinâmica, perfil de setpoint, ganhos e saturação. `common/src/plant_runtime.c` fornece as threads, a configuração, os sinais e o log CSV; `network.c`, o cliente libcurl; `solver.c`, um RK4 de passo fixo (`dt = 0,01 s`, entrada constante durante o passo). Os estados começam em zero.

## Lei de controle

PI fuzzy incremental, a cada iteração *k*:

$$
e_k = k_i\,\text{error}_k,\quad ed_k = k_p(\text{error}_k - \text{error}_{k-1}),\quad v_k = \mathrm{sat}\big(v_{k-1} + \mathrm{FIS}(e_k, ed_k)\big)
$$

com `error = setpoint − y`. Como a saída do FIS é um incremento, `ki` faz o papel de ganho integral e `kp` de ganho proporcional.

## Modelos

**Carro**: $m\dot v = F - b v$

| m | b | y | u | ki, kp |
|---|---|---|---|--------|
| 450 kg | 20 N·s/m | v (m/s) | F ∈ [−4000, 8000] N | 1/180, 5 |

**Motor CC**: $J\dot\omega = K i - b\omega - \tau_c$, $\;L\dot i = V - K\omega - R i$

| R | L | J | b | K | τc | y | u | ki, kp |
|---|---|---|---|---|----|---|---|--------|
| 0,2 Ω | 0,1 H | 0,05 kg·m² | 0,001 N·m·s | 0,5 N·m/A | 0,2 N·m | ω (rad/s) | V ∈ [−36, 36] V | 1/2000, 0,5 |

**Tanque**: $A\dot h = q_m u - V_o a \sqrt{2 g h}$, com ḣ = 0 em H_max quando a vazão líquida é positiva.

| A | a | q_m | g | V_o | H_max | y | u | ki, kp |
|---|---|-----|---|-----|-------|---|---|--------|
| 1 m² | 0,1 m² | 2 m³/s | 9,8 m/s² | 0,5 | 30 m | h (m) | comando da bomba ∈ [0, 1] | 1/30, 100 |

## Perfis de setpoint

`exp_type = 1`: constante (primeiro valor abaixo). Caso contrário, por degraus, trocando em T1, T2, T3:

| Planta | Valores | T1, T2, T3 |
|--------|---------|------------|
| carro (m/s) | 20; 41,67; 13,89; 50 | 50, 100, 150 s |
| motor CC (rad/s) | 52,36; 31,42; 62,83; 41,89 | 10, 20, 30 s |
| tanque (m) | 20; 10; 25; 15 | 50, 100, 150 s |

## Uso

```
plants/bin/<car|dc_motor|tank> [exp_type=1] [arquivo_csv=<planta>_network_log.csv]
```

| Variável (padrão) | Descrição |
|-------------------|-----------|
| `CONTROLLER_URL` (`http://localhost:5000/control`) | Endpoint do controlador. |
| `CLIENT_ID` (nome do CSV sem `.csv`) | Enviado em cada requisição; registrado pelo controlador como `IIoT_name`. |
| `REQUEST_PERIOD_MS` (`0`) | 0 = modo livre; > 0 = período fixo. |
| `DURATION_S` (`0`) | Tempo decorrido após o qual a planta para; 0 = até SIGINT/SIGTERM. |
| `HTTP_KEEPALIVE` (`0`) | 1 = reutiliza a conexão TCP. |
| `HTTP_TIMEOUT_MS` (`0`) | Timeout da requisição; 0 = nenhum. |
| `LOG_EVERY` (`1`) | Grava uma linha a cada N iterações. |

A configuração é impressa na saída padrão ao iniciar. `DURATION_S` é verificado após cada requisição. O CSV é gravado em disco a cada segundo e ao sair.
