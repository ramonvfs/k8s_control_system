# Simuladores das plantas

Três plantas de referência são simuladas em C. Todas usam o runtime em `plants/common/`; cada `plants/<nome>/main.c` só fornece um `plant_model_t` (veja `plants/common/include/plant.h`) com a dinâmica, o perfil de setpoint, os ganhos e os limites de saturação.

| Arquivo | Função |
|---------|--------|
| `common/include/plant.h`, `common/src/plant_runtime.c` | Interface do modelo; threads de simulação e de controle, configuração, tratamento de sinais, log CSV. |
| `common/include/network.h`, `common/src/network.c` | Cliente libcurl do controlador (`POST /control`). |
| `common/include/solver.h`, `common/src/solver.c` | Integrador Runge–Kutta de 4ª ordem com passo fixo. |

## Integração numérica

Cada planta é integrada pelo RK4 clássico, com passo `dt = 0,01 s` e a entrada de controle constante durante o passo (segurador de ordem zero). A thread de simulação roda em tempo real: cada passo é agendado com `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` em prazos absolutos, então o período não acumula desvio com o tempo de cálculo. A thread dorme até o prazo (não há espera ativa) e, se um prazo já passou, executa o passo imediatamente. Todos os estados começam em zero.

## Lei de controle

O controlador é um PI fuzzy incremental (forma de velocidade). A cada iteração de controle *k*:

$$
\begin{aligned}
\text{error}_k &= r_k - y_k \\
e_k &= k_i \cdot \text{error}_k \\
ed_k &= k_p \cdot (\text{error}_k - \text{error}_{k-1}) \\
s_k &= \mathrm{FIS}(e_k, ed_k) \in [-1, 1] \\
v_k &= \mathrm{sat}(v_{k-1} + s_k,\; u_{min},\; u_{max})
\end{aligned}
$$

Como a saída do FIS é um incremento, o termo em `e` faz o papel da ação integral (`ki`) e o termo na variação do erro faz o papel da ação proporcional (`kp`). O controlador limita `e` e `ed` a [−1, 1] antes da inferência. Como `error_{-1} = 0`, a primeira requisição leva `ed = kp·error_0`.

## Modelos

### Carro (`plants/car`)

Modelo longitudinal de primeira ordem:

$$ m\,\dot v = F - b\,v $$

| Símbolo | Valor | Descrição |
|---------|-------|-----------|
| m | 450 kg | Massa do veículo |
| b | 20 N·s/m | Coeficiente de amortecimento |
| y = v | m/s | Saída controlada (velocidade) |
| u = F | [−4000, 8000] N | Força de tração/frenagem |
| ki, kp | 1/180, 5 | Ganhos do erro e da variação do erro |

### Motor CC (`plants/dc_motor`)

Motor controlado pela armadura, com torque de carga constante:

$$
\begin{aligned}
J\,\dot\omega &= K\,i - b\,\omega - \tau_c \\
L\,\dot i &= V - K\,\omega - R\,i
\end{aligned}
$$

| Símbolo | Valor | Descrição |
|---------|-------|-----------|
| R | 0,2 Ω | Resistência da armadura |
| L | 0,1 H | Indutância da armadura |
| J | 0,05 kg·m² | Inércia do rotor |
| b | 0,001 N·m·s | Atrito viscoso |
| K | 0,5 N·m/A (V·s/rad) | Constante de torque / força contraeletromotriz |
| τc | 0,2 N·m | Torque de carga |
| y = ω | rad/s | Saída controlada (velocidade angular) |
| u = V | [−36, 36] V | Tensão de armadura |
| ki, kp | 1/2000, 0,5 | Ganhos do erro e da variação do erro |

> A entrada de controle entra na equação elétrica como tensão. Os logs antigos a chamavam de `ControlInput(A)`, mas a unidade é volt.

Em regime permanente, com entrada constante V, a velocidade é $\omega = (K V/R - \tau_c)/(K^2/R + b)$. Isso ajuda a reconhecer uma execução em malha aberta: com V = 25,05 V, ω ≈ 49,9 rad/s.

### Tanque (`plants/tank`)

Tanque único com vazão de entrada por bomba e saída por gravidade (Torricelli):

$$ A\,\dot h = q_m\,u - V_o\,a\,\sqrt{2 g h} $$

| Símbolo | Valor | Descrição |
|---------|-------|-----------|
| A | 1,0 m² | Seção transversal do tanque |
| a | 0,1 m² | Seção do tubo de saída |
| q_m | 2,0 m³/s | Vazão máxima da bomba |
| g | 9,8 m/s² | Gravidade |
| V_o | 0,5 | Abertura da válvula de saída (fixa) |
| H_max | 30 m | Nível máximo: em H_max, uma vazão líquida positiva é descartada (ḣ = 0) |
| y = h | m | Saída controlada (nível) |
| u | [0, 1] | Comando da bomba (vazão de entrada = q_m·u) |
| ki, kp | 1/30, 100 | Ganhos do erro e da variação do erro |

Níveis negativos causados por erro numérico são tratados como 0 dentro da raiz quadrada.

## Perfis de setpoint

Escolhidos pelo primeiro argumento da linha de comando (`exp_type`). O tipo 1 é uma referência constante; qualquer outro valor seleciona o perfil por degraus abaixo (`t` é o tempo de simulação).

| Planta | Tipo 1 | Tipo 2: 0 < t ≤ T1 | T1 < t ≤ T2 | T2 < t ≤ T3 | t > T3 | (T1, T2, T3) |
|--------|--------|-------------------|-------------|-------------|--------|--------------|
| carro (m/s) | 20,0 | 20,0 (72 km/h) | 41,67 (150 km/h) | 13,89 (50 km/h) | 50,0 (180 km/h) | (50, 100, 150) s |
| motor CC (rad/s) | 52,36 | 52,36 (500 RPM) | 31,42 (300 RPM) | 62,83 (600 RPM) | 41,89 (400 RPM) | (10, 20, 30) s |
| tanque (m) | 20,0 | 20,0 | 10,0 | 25,0 | 15,0 | (50, 100, 150) s |

Para observar o perfil completo, a execução precisa durar mais que T3 mais o tempo de acomodação do último degrau (por exemplo, 200 s para o carro e o tanque, 40 s para o motor). A constante de tempo do carro é m/b = 22,5 s.

## Executando uma planta

```
plants/bin/<car|dc_motor|tank> [exp_type] [arquivo_csv]
```

| Argumento | Padrão | Descrição |
|-----------|--------|-----------|
| `exp_type` | `1` | Perfil de setpoint (acima). |
| `arquivo_csv` | `<planta>_network_log.csv` | CSV de saída (esquema em [data_schema.md](data_schema.md)). |

Variáveis de ambiente (padrão entre parênteses; o `scripts/config.env` as define para `make run_*`):

| Variável | Descrição |
|----------|-----------|
| `CONTROLLER_URL` (`http://localhost:5000/control`) | Endpoint do controlador. |
| `CLIENT_ID` (nome do CSV sem `.csv`) | Identificador enviado em cada requisição e registrado pelo controlador como `IIoT_name`. O padrão deixa a rodada, M, K e o índice do cliente visíveis no log do controlador quando os arquivos seguem a convenção `res_<planta>_M<m>_K<k>_EXP<n>_client<i>.csv`. |
| `REQUEST_PERIOD_MS` (`0`) | 0 = modo livre; > 0 = período fixo entre requisições (veja [architecture.md](architecture.md#ritmo-das-requisições)). |
| `DURATION_S` (`0`) | Encerra depois de tantos segundos de malha de controle (tempo real decorrido); 0 = até SIGINT/SIGTERM. |
| `HTTP_KEEPALIVE` (`0`) | 0 = conexão TCP nova por requisição; 1 = reutiliza a conexão. |
| `HTTP_TIMEOUT_MS` (`0`) | Timeout da requisição; 0 = nenhum. Uma requisição que estoura o timeout conta como falha (`s = 0`). |
| `LOG_EVERY` (`1`) | Grava uma linha de CSV a cada N iterações. |

Na inicialização, a planta imprime toda a sua configuração (modelo, ganhos, limites, rede) na saída padrão; redirecionar a saída para um `.log` ao lado do CSV guarda os metadados de cada execução.

### Encerramento

A planta termina quando `DURATION_S` se esgota ou ao receber SIGINT/SIGTERM (por exemplo, `timeout 215 plants/bin/car 2 ...`). A duração é medida em tempo real decorrido desde o início da thread de controle e é verificada após cada requisição; sem `HTTP_TIMEOUT_MS`, uma requisição presa atrasa o encerramento. O CSV usa buffer em bloco e é gravado em disco uma vez por segundo e na saída; com SIGKILL, perde-se no máximo o último segundo de dados. Uma requisição em andamento é concluída antes de sair.
