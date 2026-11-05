# Controlador fuzzy

[English](../controller.md)

`controllers/fuzzy_cpp` (C++: inferência em `fuzzy_fim.hpp`, log assíncrono em `csv_logger.hpp`, servidor HTTP em `main.cpp`) e `controllers/fuzzy_py` (Python) implementam o mesmo sistema, API e formato de log. As saídas diferem em menos de 10⁻³ (discretização da defuzzificação).

## Sistema de inferência

Mamdani, entradas `error` e `delta_error` limitadas a [−1, 1], saída em [−1, 1]. E = mínimo, implicação = mínimo, agregação = máximo, defuzzificação por centroide (100 amostras), saída 0 quando nenhuma regra dispara.

Funções de pertinência (iguais para todas as variáveis):

| LN | MN | SN | ZZ | SP | MP | LP |
|----|----|----|----|----|----|----|
| trap(−1; −1; −0,7; −0,4) | tri(−0,7; −0,4; −0,1) | tri(−0,4; −0,1; 0) | tri(−0,1; 0; 0,1) | tri(0; 0,1; 0,4) | tri(0,1; 0,4; 0,7) | trap(0,4; 0,7; 1; 1) |

Regras (linha: error, coluna: delta_error):

| | LN | MN | SN | ZZ | SP | MP | LP |
|-|----|----|----|----|----|----|----|
| **LN** | LN | LN | LN | LN | MN | SN | ZZ |
| **MN** | LN | LN | LN | MN | SN | ZZ | SP |
| **SN** | LN | LN | MN | SN | ZZ | SP | MP |
| **ZZ** | LN | MN | SN | ZZ | SP | MP | LP |
| **SP** | MN | SN | ZZ | SP | MP | LP | LP |
| **MP** | SN | ZZ | SP | MP | LP | LP | LP |
| **LP** | ZZ | SP | MP | LP | LP | LP | LP |

## API

- `POST /control` `{"error", "delta_error", "client_id", "seq"}` → `{"u", "pod"}` (`u` primeiro, para plantas antigas). Entrada inválida → 400.
- `GET /health` → `{"status": "ok", "pod"}`.

## Concorrência

A cpp-httplib atende as requisições com um pool de threads (padrão `max(8, nCPU − 1)`, onde nCPU é o número de CPUs do nó, mesmo dentro de um contêiner; `HTTP_THREADS` substitui). O motor do FuzzyLite guarda entradas e resultados intermediários nos próprios objetos e não é thread-safe, então a inferência é serializada por um mutex: **uma inferência por vez por pod**; a vazão escala com réplicas. Outros padrões relevantes da cpp-httplib: backlog de `listen` igual a 5, timeout de keep-alive de 5 s. A versão Python (servidor de desenvolvimento do Flask, dezenas de ms por inferência) é uma referência e não serve para malhas de 10 ms.

## Log

Um CSV por pod, uma linha por requisição ([esquema](data_schema.md#log-do-controlador)). Medições dentro do handler:

```
entrada ── leitura ── espera mutex ── inferência ── resposta
          t_parsed    └LockWait_us─┘└Inference_us┘
└──────────────────── Handler_us ──────────────┘
```

As linhas ficam em memória e são gravadas por uma thread em segundo plano a cada segundo e no SIGTERM. Nome do arquivo: `Result_CSS_<início, UTC>[_Exp_<EXP_NAME>]_pod_<POD_NAME>.csv`.

| Variável (padrão) | Descrição |
|-------------------|-----------|
| `PORT` (`5000`) | Porta de escuta. |
| `POD_NAME` (hostname) | Nome da réplica no log e na resposta. |
| `NODE_NAME` (`-`) | Exibido ao iniciar. |
| `CONTROLLER_LOG` (`1`) | 0 desativa o log. |
| `LOG_DIR` (`.`) | Diretório do log. |
| `EXP_NAME` (vazio) | Marcador opcional no nome do arquivo. |
| `HTTP_THREADS` (padrão da biblioteca) | Threads de atendimento (C++). |
| `RATE_WINDOW_MS` (`1000`) | Janela do `Request_rate`. |
