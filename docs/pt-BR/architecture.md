# Arquitetura

[English](../architecture.md)

## Componentes

| Componente | Onde roda | Código |
|------------|-----------|--------|
| Plantas | Máquina geradora de carga; *M* processos, um por cliente | `plants/` |
| Controlador fuzzy | Deployment do Kubernetes, *K* réplicas, Service NodePort `:30005` | `controllers/fuzzy_cpp/` |
| Coletor de métricas | DaemonSet, CPU/rede dos pods com label `app=fuzzy-ctrl` | imagem `ramonvfs/fastcollector:v1` |

O controlador não guarda estado: cada requisição leva tudo o que é necessário (a planta mantém o erro anterior e a entrada de controle integrada), então qualquer réplica atende qualquer requisição.

## Malha de controle

Cada planta roda duas threads que compartilham o estado por um mutex:

```
 thread de simulação (dt = 10 ms)           thread de controle
   u <- shared.u                             y, r <- shared
   x <- RK4(x, u, dt); t += dt               e  = ki·(r − y)
   r <- setpoint(t)                          ed = kp·((r − y) − erro_anterior)
   shared <- x, t, r                         s  = POST /control {e, ed, client_id, seq}
   dorme até o próximo prazo                 v  = sat(v + s); shared.u <- v; linha no CSV
```

A planta é integrada em tempo real, independentemente da rede: a entrada é mantida até a próxima resposta, e uma requisição com falha dá `s = 0`. A thread de simulação dorme até prazos absolutos (`clock_nanosleep`, `TIMER_ABSTIME`), então o tempo de cálculo não se acumula como desvio; passos atrasados rodam em sequência até a simulação alcançar o relógio.

### Ritmo das requisições

- **Modo livre** (`REQUEST_PERIOD_MS=0`, padrão): a próxima requisição sai assim que a resposta chega; uma requisição em andamento por planta.
- **Período fixo** (`REQUEST_PERIOD_MS=P`): requisições a cada *P* ms; após um atraso, o cronograma recomeça do instante atual, sem rajadas.

No modo livre a taxa da malha é 1/RTT. Como a saída do FIS é um incremento e `y` só muda a cada `dt`, o ganho integral efetivo cresce com 1/RTT quando RTT < dt: o comportamento em malha fechada passa a depender da latência e da carga. Use `REQUEST_PERIOD_MS=10` (= dt) quando o desempenho de controle precisar ser comparável entre configurações.

## Kubernetes

- **Deployment**: request de 100m e limite de 200m de CPU. `POD_NAME` e `NODE_NAME` vêm da Downward API. Readiness probe `GET /health`. Ao receber SIGTERM, o pod para de aceitar requisições e grava o log restante (`terminationGracePeriodSeconds: 30`).
- **Service**: o kube-proxy balanceia **conexões**. Por padrão as plantas abrem uma conexão por requisição (`HTTP_KEEPALIVE=0`), então as requisições se distribuem entre as réplicas; com keep-alive, a planta fica numa réplica só. Por causa do SNAT o IP de origem não é significativo; as plantas se identificam pelo `client_id`.
- **Logs**: `hostPath` `/tmp/dataset/fuzzy-ctrl`, um arquivo por pod.

### Esgotamento de portas efêmeras

A planta fecha cada conexão após a resposta, então o socket fica 60 s em `TIME_WAIT` na máquina geradora de carga (valor fixo no kernel; o `tcp_fin_timeout` não o altera). Com o intervalo de portas padrão (`32768–60999`, 28 232 portas), a máquina sustenta cerca de 28 232 / 60 ≈ **470 conexões novas/s** para um destino, somando todas as plantas. A taxa de requisições é `M / RTT` (modo livre) ou `M × 1000 / REQUEST_PERIOD_MS`. Acima do limite, as requisições novas falham (`ok = 0`, `curlCode` ≠ 0) até os sockets antigos expirarem.

Correção na máquina geradora de carga:

```bash
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"
sudo sysctl -w net.ipv4.tcp_tw_reuse=1      # reutiliza portas em TIME_WAIT em conexões de saída (exige tcp_timestamps=1, o padrão)
```

Para tornar permanente, use `/etc/sysctl.d/99-loadgen.conf`. Para acompanhar: `ss -Htan state time-wait | wc -l`. Alternativas: `HTTP_KEEPALIVE=1` (mas a carga deixa de ser distribuída por requisição) ou vários IPs de origem. Em taxas altas, verifique também `nf_conntrack_count` / `nf_conntrack_max` nos nós.

## Relógios

| Colunas | Relógio | Observação |
|---------|---------|------------|
| `TimestampFormat_us`, `Timestamp_us` | tempo real, UTC | Comparável entre máquinas só com NTP. |
| RTT, tempos de laço e de atendimento, `ElapsedTime_us` | monotônico | Durações; imunes a ajustes do relógio. |
| `t` | tempo de simulação | Por planta, começa em 0. |
