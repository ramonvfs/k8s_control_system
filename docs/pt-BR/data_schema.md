# Esquema dos dados

[English](../data_schema.md)

Arquivos CSV com uma linha de cabeçalho. `TimestampFormat_us` (`AAAA-MM-DD HH:MM:SS.ffffff`) e `Timestamp_us` (µs desde a época Unix) são o mesmo instante, em UTC.

## Log da planta

Uma linha por requisição (ou a cada `LOG_EVERY`).

| Coluna | Unidade | Descrição |
|--------|---------|-----------|
| `TimestampFormat_us`, `Timestamp_us` | – | Instante de envio da requisição. |
| `t` | s | Tempo de simulação do estado usado. |
| `seq` | – | Índice da requisição a partir de 0, enviado ao controlador. |
| `setpoint`, `y`, `error` | planta¹ | Referência, saída, setpoint − y. |
| `e`, `ed` | – | Entradas normalizadas enviadas: `ki·error`, `kp·Δerror`. |
| `s` | – | Saída do controlador (0 em caso de falha). |
| `v` | entrada¹ | Entrada de controle aplicada. |
| `requestDelay` | ms | Tempo de ida e volta da transação HTTP. |
| `connectTime` | ms | Até a conexão TCP ser estabelecida (0 se reutilizada). |
| `firstByteTime` | ms | Até o primeiro byte da resposta (inclui `connectTime`). |
| `measuredLoopTime` | ms | Tempo desde o início da iteração anterior. |
| `requestTime` | ms | `REQUEST_PERIOD_MS` configurado (0 = modo livre). |
| `httpStatus`, `curlCode` | – | Status HTTP (0 = sem resposta) e código da libcurl (0 = OK). |
| `ok` | 0/1 | A requisição deu certo e `u` foi lido. |
| `pod` | – | Réplica que respondeu. |

¹ carro: m/s, N · motor CC: rad/s, V · tanque: m, 0–1.

## Log do controlador

| Coluna | Unidade | Descrição |
|--------|---------|-----------|
| `TimestampFormat_us`, `Timestamp_us` | – | Instante em que a requisição chegou ao handler. |
| `ElapsedTime_us` | µs | Desde o início do processo. |
| `Pod` | – | Nome da réplica. |
| `IIoT_name`, `seq` | – | `client_id` e `seq` enviados pela planta (`-`, −1 se ausentes). |
| `e`, `ed`, `s` | – | Entradas como recebidas (antes da limitação) e saída. |
| `Request_rate` | req/s | Requisições neste pod nos últimos `RATE_WINDOW_MS`. |
| `LockWait_us` | µs | Espera pelo mutex da inferência. |
| `Inference_us` | µs | Cálculo do FIS. |
| `Handler_us` | µs | Da entrada no handler até a resposta pronta. |

## Cruzamento

```python
joined = plant.assign(IIoT_name=client_id).merge(ctrl, on=["IIoT_name", "seq"])
joined["network_ms"] = joined.requestDelay - joined.Handler_us / 1000
```

O `seq` recomeça em cada processo de planta, então o `client_id` precisa ser único por execução.
