# Arquitetura

## Componentes

| Componente | Onde roda | Implementação |
|------------|-----------|---------------|
| **Plantas** (carro, motor CC, tanque) | Máquina geradora de carga, fora do cluster. Até *M* processos rodam em paralelo, um por cliente simulado. | C, `plants/` |
| **Controlador fuzzy** (`fuzzy-ctrl-cpp`) | Deployment do Kubernetes com *K* réplicas, exposto por um Service NodePort (`:30005`). | C++17, `controllers/fuzzy_cpp/` |
| **Coletor de métricas** (`fastcollector`) | DaemonSet, um pod por nó; amostra CPU e rede dos pods do controlador (label `app=fuzzy-ctrl`). | Imagem externa `ramonvfs/fastcollector:v1` |

O controlador não guarda estado: cada planta mantém o estado da própria malha (erro anterior, entrada de controle integrada) e envia em cada requisição tudo o que o controlador precisa. Por isso qualquer réplica pode atender qualquer requisição, e é isso que permite o escalonamento horizontal.

## Malha de controle pela rede

Cada processo de planta roda duas threads POSIX, que compartilham o estado da planta por meio de um mutex:

```
 thread de simulação (período dt = 10 ms, prazos absolutos em CLOCK_MONOTONIC)
   u   <- shared.u
   x   <- RK4(x, u, dt)            t <- t + dt
   r   <- setpoint(exp_type, t)
   shared.{x, t, setpoint} <- x, t, r

 thread de controle (modo livre ou período fixo REQUEST_PERIOD_MS)
   y, r, t <- shared
   error   = r - y
   e       = ki * error
   ed      = kp * (error - error_anterior)
   s       = POST /control {e, ed, client_id, seq}     <- ida e volta pela rede
   v       = sat(v + s, u_min, u_max)
   shared.u <- v
   linha no CSV
```

A planta é integrada em tempo real, independentemente da rede. Assim, o atraso de rede aparece como apareceria numa planta física: a entrada aplicada à planta é mantida (segurador de ordem zero) até a próxima resposta chegar. Se uma requisição falha, `s = 0` e a última entrada é mantida.

A espera da thread de simulação é um *sleep* até um instante absoluto (`clock_nanosleep` com `TIMER_ABSTIME`), e não uma espera ativa. Como cada prazo é calculado a partir do anterior, o tempo gasto no cálculo não se acumula como desvio. Se um prazo já passou, a chamada retorna na hora e os passos atrasados rodam em sequência até a simulação alcançar o relógio.

### Ritmo das requisições

- **Modo livre** (`REQUEST_PERIOD_MS=0`, padrão, comportamento dos experimentos originais): a próxima requisição é enviada assim que a resposta anterior chega. Nunca há duas requisições da mesma planta em andamento, e a taxa de amostragem da malha é 1/RTT.
- **Período fixo** (`REQUEST_PERIOD_MS=P`): as requisições começam a cada *P* ms, em prazos absolutos. Se uma requisição demora mais que *P*, a seguinte começa imediatamente e o cronograma é reiniciado a partir desse instante; os horários perdidos não são compensados com rajadas.

> **Observação sobre o modo livre.** A saída do FIS é um *incremento* da entrada de controle, e o estado da simulação só muda a cada `dt`. Com RTT < dt, várias requisições veem o mesmo `y`: a primeira leva toda a variação do erro e as seguintes têm `ed = 0`, mas cada uma ainda soma um incremento proporcional a `e`. O ganho integral efetivo por unidade de tempo cresce, portanto, com 1/RTT: a dinâmica em malha fechada passa a depender da latência da rede e da carga do cluster. Numa máquina local (RTT ≈ 0,15 ms, ~66 requisições por passo de 10 ms) a malha do motor CC diverge, tanto com o código original quanto com o refatorado. Quando o desempenho de controle precisa ser comparável entre configurações (M, K), use `REQUEST_PERIOD_MS=10` (= dt); o modo livre serve como teste de vazão e de estresse.

## Implantação no Kubernetes

O `fuzzy-ctrl.yaml` define:

- **Deployment `fuzzy-ctrl-cpp`**: request de 100m e limite de 200m de CPU por pod. O nome do pod e o nome do nó são injetados pela Downward API (`POD_NAME`, `NODE_NAME`) e usados no log do controlador. Uma readiness probe em `GET /health` mantém réplicas novas fora do Service até que consigam responder. Ao reduzir o número de réplicas, o pod recebe SIGTERM, para de aceitar requisições e grava o log restante dentro de `terminationGracePeriodSeconds` (30 s).
- **Service `fuzzy-service`** (NodePort 30005 → porta 5000 do contêiner). O kube-proxy balanceia **conexões**, e não requisições HTTP. Como as plantas abrem uma conexão TCP nova por requisição por padrão (`HTTP_KEEPALIVE=0`), as requisições sucessivas de uma planta se distribuem entre as réplicas. Com keep-alive ativado, a planta ficaria na réplica escolhida para a primeira conexão.
- **Volume de log**: `hostPath` `/tmp/dataset/fuzzy-ctrl` no nó, montado em `/var/log/fuzzy-ctrl`. Cada pod grava o próprio `Result_CSS_*_pod_<pod>.csv`, então os arquivos de réplicas diferentes nunca colidem. O DaemonSet `fastcollector` grava em `/tmp/dataset` no mesmo nó.

### Esgotamento de portas efêmeras no gerador de carga

Abrir uma conexão TCP por requisição tem um custo na máquina que roda as plantas. A planta (libcurl) fecha cada conexão logo após a resposta, então é o lado da planta que faz o *fechamento ativo*, e cada socket fica 60 s em `TIME_WAIT` (`TCP_TIMEWAIT_LEN`, valor fixo no kernel Linux; o `tcp_fin_timeout` **não** reduz esse tempo). Cada socket em `TIME_WAIT` mantém sua porta local reservada para o mesmo destino (IP, porta). Com o intervalo padrão `net.ipv4.ip_local_port_range = 32768 60999` (28 232 portas), a taxa sustentável para um destino é de cerca de

$$ \frac{28\,232 \text{ portas}}{60 \text{ s}} \approx 470 \text{ conexões novas/s} $$

para a máquina geradora de carga inteira, isto é, somando as *M* plantas. Acima dessa taxa, as portas acabam depois de `28 232 / taxa` segundos e as requisições novas falham: a planta as registra com `ok = 0` e `curlCode` diferente de zero (observado: 56, *failure when receiving data from the peer*; 7 ou 99 também são possíveis) e mantém sua entrada de controle (`s = 0`). As falhas duram até os sockets mais antigos em `TIME_WAIT` expirarem; em execuções com mais de 60 s, elas aparecem e somem em rajadas.

Observado num teste local (3 contêineres do controlador atrás de um nginx, 4 plantas em modo livre, ~670 req/s no total): o número de sockets em `TIME_WAIT` cresceu linearmente e parou em 28 235. A partir de t ≈ 23 s praticamente todas as requisições falharam, e as plantas ficaram em malha aberta até o fim da execução de 60 s. No cluster, a taxa é `M / RTT` no modo livre ou `M × 1000 / REQUEST_PERIOD_MS` com período fixo (por exemplo, M = 32 com 10 ms → 3 200 conexões/s), então o limite é atingido com poucos clientes.

As versões das plantas que gravavam só `Timestamp(s),RTT(ms),Setpoint,Output,ControlInput` escondiam essas falhas: uma requisição com falha aparecia como uma linha normal (só o stderr mostrava `Network Error`). Verifique `ok` e `curlCode` antes de usar uma execução.

**Correção: ajustar a máquina geradora de carga** (exige root; nada muda no cluster):

```bash
# Usa (quase) todo o intervalo de portas para conexões de saída: ~64 500 portas.
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# Permite que uma conexão de saída nova reutilize uma porta em TIME_WAIT
# quando os timestamps TCP garantem que segmentos antigos não serão
# confundidos com a conexão nova. Padrão 2 = só loopback; 1 = todas.
sudo sysctl -w net.ipv4.tcp_tw_reuse=1

# Exigido pelo tcp_tw_reuse (já vale 1 por padrão).
sudo sysctl -w net.ipv4.tcp_timestamps=1
```

Para manter os ajustes após reiniciar, grave-os em `/etc/sysctl.d/99-loadgen.conf` e rode `sudo sysctl --system`. Com `tcp_tw_reuse = 1`, uma porta pode ser reutilizada quando o seu socket em `TIME_WAIT` tiver mais que `net.ipv4.tcp_tw_reuse_delay` (1 s por padrão, nos kernels que têm esse parâmetro), o que eleva o limite muito acima das taxas do experimento. Não procure o `tcp_tw_recycle`: ele era inseguro atrás de NAT e foi removido no Linux 4.12.

Alternativas, cada uma com um efeito colateral sobre o que está sendo medido:

| Opção | Efeito |
|-------|--------|
| `HTTP_KEEPALIVE=1` | Uma conexão por planta, sem acúmulo de `TIME_WAIT`. Mas o kube-proxy balanceia conexões: cada planta fica presa a uma réplica, e adicionar réplicas deixa de distribuir a carga de uma planta. |
| `REQUEST_PERIOD_MS` > 0 | Limita a taxa a `M × 1000 / REQUEST_PERIOD_MS`; sozinho, só basta para M pequeno. |
| Vários IPs de origem no gerador de carga | Cada par (IP de origem, destino) tem o seu próprio intervalo de portas. |

Para verificar uma máquina durante uma execução:

```bash
ss -Htan state time-wait | wc -l          # sockets em TIME_WAIT (limite = tamanho do intervalo de portas)
sysctl net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse
```

Os nós do cluster mantêm o estado de NAT de cada conexão do Service na tabela conntrack. Com milhares de conexões por segundo, vale também verificar `sysctl net.netfilter.nf_conntrack_count net.netfilter.nf_conntrack_max` nos nós; com a tabela cheia, conexões novas são descartadas.

### Identificação da planta

Com `externalTrafficPolicy: Cluster` (o padrão), uma requisição que chega a um nó pode ser encaminhada a um pod de outro nó e tem o endereço de origem traduzido (SNAT). O IP de origem visto pelo pod é então o de um nó, e não o da máquina das plantas. Por isso a identidade da planta é enviada no corpo da requisição (`client_id`), e o endereço IP não é registrado.

## Relógios e bases de tempo

| Grandeza | Relógio | Comparável entre máquinas? |
|----------|---------|----------------------------|
| `TimestampFormat_us` (UTC), `Timestamp_us` (plantas e controlador) | `CLOCK_REALTIME` / `system_clock` | Só se as máquinas estiverem sincronizadas (NTP/chrony). O fuso horário não importa: as duas colunas estão em UTC. |
| RTT, tempo de laço, tempos de atendimento, `ElapsedTime_us` | `CLOCK_MONOTONIC` / `steady_clock` | Só durações; imunes a ajustes do relógio. |
| `t` (plantas) | Tempo de simulação, `k·dt` | Por planta, começa em 0. |

Durações devem ser sempre calculadas a partir das colunas monotônicas; as colunas de relógio de parede servem para alinhar séries de fontes diferentes (plantas, pods do controlador, fastcollector).
