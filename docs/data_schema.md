# Esquema dos dados

Uma execução produz três tipos de arquivo:

| Arquivo | Produtor | Granularidade |
|---------|----------|---------------|
| `res_<planta>_M<m>_K<k>_EXP<n>_client<i>.csv` (nome escolhido pelo script de execução) | Cada processo de planta | Uma linha por requisição de controle (ou a cada `LOG_EVERY`). |
| `Result_CSS_<início>[_Exp_<nome>]_pod_<pod>.csv` | Cada réplica do controlador | Uma linha por requisição atendida. |
| `metrics*.csv` | DaemonSet `fastcollector`, por nó | Amostras periódicas de CPU e rede (ferramenta externa). |

Todos os arquivos são separados por vírgula e têm uma única linha de cabeçalho. Os instantes de relógio de parede são gravados como data/hora em UTC (`TimestampFormat_us`) e como microssegundos desde a época Unix (`Timestamp_us`); as duas colunas se referem ao mesmo instante. UTC é usado em todos os arquivos para que os dados de máquinas e contêineres com fusos diferentes se alinhem (contêineres usam UTC por padrão; o gerador de carga normalmente não). O conjunto de dados de referência usava hora local em `TimestampFormat_us`.

## Log da planta (`res_*.csv`)

| Coluna | Unidade | Descrição |
|--------|---------|-----------|
| `TimestampFormat_us` | `AAAA-MM-DD HH:MM:SS.ffffff` | Instante (UTC) em que a requisição foi enviada. |
| `Timestamp_us` | µs desde a época | Mesmo instante, época Unix (UTC). |
| `t` | s | Tempo de simulação do estado usado nesta requisição (múltiplo de `dt`). |
| `seq` | – | Índice da requisição, a partir de 0. Enviado ao controlador. |
| `setpoint` | unidade da planta¹ | Referência r. |
| `y` | unidade da planta¹ | Saída medida (velocidade, velocidade angular ou nível). |
| `error` | unidade da planta¹ | r − y. |
| `e` | – | Erro normalizado enviado ao controlador, `ki·error`. |
| `ed` | – | Variação normalizada do erro enviada, `kp·(error − error_anterior)`. |
| `s` | – | Saída do controlador (incremento). 0 quando a requisição falhou. |
| `v` | unidade da entrada¹ | Entrada de controle aplicada à planta após a atualização, `sat(v_anterior + s)`. |
| `requestDelay` | ms | Tempo de ida e volta da requisição, medido com `CLOCK_MONOTONIC` em torno de toda a transação HTTP (conexão TCP, requisição, processamento no servidor, resposta). |
| `connectTime` | ms | `CONNECT_TIME` da libcurl: do início da transação até a conexão TCP ser estabelecida (0 quando a conexão foi reutilizada). |
| `firstByteTime` | ms | `STARTTRANSFER_TIME` da libcurl: do início até o primeiro byte da resposta (inclui o `connectTime`). |
| `measuredLoopTime` | ms | Intervalo entre o início desta iteração e o da anterior (0 na primeira linha). |
| `requestTime` | ms | Período de requisição configurado (`REQUEST_PERIOD_MS`); 0 = modo livre. |
| `httpStatus` | – | Código de status HTTP; 0 se nenhuma resposta foi recebida. |
| `curlCode` | – | `CURLcode` da libcurl (0 = OK, 7 = conexão recusada, 28 = timeout, 56 = falha ao receber dados, …). |
| `ok` | 0/1 | 1 se a transferência deu certo, o status foi 200 e `u` foi lido. |
| `pod` | – | Réplica do controlador que atendeu a requisição (vem da resposta). Vazio quando `ok = 0`. |

¹ Unidades por planta:

| Planta | `setpoint`, `y`, `error` | `v` |
|--------|--------------------------|-----|
| carro | m/s | N (força de tração) |
| motor CC | rad/s | V (tensão de armadura) |
| tanque | m | – (comando da bomba, 0–1) |

Grandezas derivadas:

- Tempo de rede + pilha do servidor de uma requisição: `requestDelay − Handler_us/1000` (cruzando com o log do controlador, abaixo).
- Requisições por passo de simulação: `dt / measuredLoopTime` (veja a observação sobre o modo livre em [architecture.md](architecture.md#ritmo-das-requisições)).
- Taxa de falhas: `1 − média(ok)`. Vale olhar também a distribuição no tempo: o esgotamento de portas faz as falhas se concentrarem em trechos contínuos (veja [architecture.md](architecture.md#esgotamento-de-portas-efêmeras-no-gerador-de-carga)).

## Log do controlador (`Result_CSS_*.csv`)

| Coluna | Unidade | Descrição |
|--------|---------|-----------|
| `TimestampFormat_us` | `AAAA-MM-DD HH:MM:SS.ffffff` | Instante (UTC) em que a requisição chegou ao handler. |
| `Timestamp_us` | µs desde a época | Mesmo instante, época Unix (UTC). |
| `ElapsedTime_us` | µs | Tempo desde o início do processo do controlador (monotônico). |
| `Pod` | – | Nome da réplica (`POD_NAME`). |
| `IIoT_name` | – | `client_id` enviado pela planta (`-` se ausente). |
| `seq` | – | `seq` enviado pela planta (−1 se ausente). |
| `e`, `ed` | – | Entradas como recebidas (antes da limitação a [−1, 1]). |
| `s` | – | Saída do FIS devolvida. |
| `Request_rate` | req/s | Requisições atendidas por este pod nos últimos `RATE_WINDOW_MS` (padrão 1 s), incluindo a atual. |
| `LockWait_us` | µs | Tempo de espera pelo mutex da inferência, isto é, fila atrás de outras requisições no mesmo pod. |
| `Inference_us` | µs | Tempo de cálculo do FIS. |
| `Handler_us` | µs | Da entrada no handler até a resposta pronta (leitura do JSON + espera + inferência). Não inclui o accept do TCP, a fila do pool de threads, o parsing HTTP da biblioteca do servidor nem a escrita no socket. |

## Cruzando linhas da planta e do controlador

Cada requisição é identificada por `(id do cliente, seq)`:

```python
import glob, pandas as pd

ctrl = pd.concat(pd.read_csv(f) for f in glob.glob("**/Result_CSS_*.csv", recursive=True))
plant = pd.read_csv("res_dc_motor_M4_K2_EXP2_client1.csv")
plant["IIoT_name"] = "res_dc_motor_M4_K2_EXP2_client1"   # = CLIENT_ID usado na execução

joined = plant.merge(ctrl, on=["IIoT_name", "seq"], suffixes=("_plant", "_ctrl"))
joined["network_ms"] = joined["requestDelay"] - joined["Handler_us"] / 1000
```

Linhas da planta com `ok = 0` não têm correspondente no controlador quando a requisição nem chegou ao handler. O `seq` recomeça em 0 a cada processo de planta, então o `client_id` precisa ser único por execução (o padrão, derivado do nome do CSV, é).

## Correspondência com o conjunto de dados de referência

Mapeamento entre as colunas do experimento de referência (`monitors_var_iiot-*.csv` e `Result_CSS_*.csv`) e os arquivos gerados aqui:

| Coluna de referência | Este projeto |
|----------------------|--------------|
| monitors_var: `TimestampFormat_us` | `TimestampFormat_us` (em UTC, + `Timestamp_us`) |
| monitors_var: `t` | `t` |
| monitors_var: `e`, `ed` | `e`, `ed` |
| monitors_var: `setpoint`, `y`, `v` | `setpoint`, `y`, `v` (unidades SI, por exemplo rad/s em vez de RPM) |
| monitors_var: `requestDelay` | `requestDelay` |
| monitors_var: `measuredLoopTime` | `measuredLoopTime` |
| monitors_var: `requestTime` | `requestTime` |
| Result_CSS: `TimestampForm_us` | `TimestampFormat_us` (em UTC) |
| Result_CSS: `ElapsedTime_us` | `ElapsedTime_us` |
| Result_CSS: `IIoT_name` | `IIoT_name` |
| Result_CSS: `e`, `ed`, `s` | `e`, `ed`, `s` |
| Result_CSS: `Request_rate` | `Request_rate` |

Colunas adicionais aqui: `seq`, `error`, `s`, `connectTime`, `firstByteTime`, `httpStatus`, `curlCode`, `ok`, `pod` (planta); `Timestamp_us`, `Pod`, `seq`, `LockWait_us`, `Inference_us`, `Handler_us` (controlador). A referência também expunha as variáveis da planta num endpoint Prometheus; isso não foi implementado aqui.

## Correspondência com o formato anterior do log das plantas

As versões anteriores gravavam `Timestamp(s),RTT(ms),Setpoint(<unidade>),<Velocity|Level>(<unidade>),ControlInput(<unidade>)`:

| Coluna anterior | Coluna nova |
|-----------------|-------------|
| `Timestamp(s)` (tempo desde o início da thread de controle) | `t` (tempo de simulação) ou `Timestamp_us` |
| `RTT(ms)` | `requestDelay` |
| `Setpoint(...)` | `setpoint` |
| `Velocity(...)` / `Level(m)` | `y` |
| `ControlInput(...)` | `v` |
