# Serviço do controlador fuzzy

Há duas implementações do mesmo controlador. A em C++ (`controllers/fuzzy_cpp`) é a implantada nos experimentos; a em Python (`controllers/fuzzy_py`) é uma implementação de referência com a mesma API e o mesmo formato de log.

| Arquivo | Função |
|---------|--------|
| `fuzzy_cpp/src/fuzzy_fim.hpp` | Máquina de inferência fuzzy (motor FuzzyLite). |
| `fuzzy_cpp/src/csv_logger.hpp` | Gravador de CSV assíncrono. |
| `fuzzy_cpp/src/main.cpp` | Servidor HTTP, configuração, contabilidade das requisições, encerramento. |
| `fuzzy_py/control.py` | Implementação em Python (scikit-fuzzy + Flask). |

## Máquina de inferência fuzzy (FIM)

Sistema Mamdani com duas entradas e uma saída, todas no universo normalizado [−1, 1].

| Propriedade | Valor |
|-------------|-------|
| Entradas | `error` (e), `delta_error` (ed); limitadas a [−1, 1] |
| Saída | `output` (s), incremento da entrada de controle |
| Termos por variável | 7: LN, MN, SN, ZZ, SP, MP, LP |
| Regras | 49 (tabela 7×7 completa) |
| Conjunção (E) | Mínimo |
| Implicação | Mínimo |
| Agregação | Máximo |
| Ativação | General (todas as regras disparam; padrão do FuzzyLite) |
| Defuzzificação | Centroide, 100 amostras (FuzzyLite) / universo de 101 pontos (scikit-fuzzy) |
| Saída padrão | 0 (nenhuma regra ativada) |

Funções de pertinência (iguais para as três variáveis):

| Termo | Forma | Parâmetros |
|-------|-------|------------|
| LN (grande negativo) | Trapézio | −1,0; −1,0; −0,7; −0,4 |
| MN (médio negativo) | Triângulo | −0,7; −0,4; −0,1 |
| SN (pequeno negativo) | Triângulo | −0,4; −0,1; 0,0 |
| ZZ (zero) | Triângulo | −0,1; 0,0; 0,1 |
| SP (pequeno positivo) | Triângulo | 0,0; 0,1; 0,4 |
| MP (médio positivo) | Triângulo | 0,1; 0,4; 0,7 |
| LP (grande positivo) | Trapézio | 0,4; 0,7; 1,0; 1,0 |

Tabela de regras (`se error é <linha> e delta_error é <coluna> então output é <célula>`):

| e \ ed | LN | MN | SN | ZZ | SP | MP | LP |
|--------|----|----|----|----|----|----|----|
| **LN** | LN | LN | LN | LN | MN | SN | ZZ |
| **MN** | LN | LN | LN | MN | SN | ZZ | SP |
| **SN** | LN | LN | MN | SN | ZZ | SP | MP |
| **ZZ** | LN | MN | SN | ZZ | SP | MP | LP |
| **SP** | MN | SN | ZZ | SP | MP | LP | LP |
| **MP** | SN | ZZ | SP | MP | LP | LP | LP |
| **LP** | ZZ | SP | MP | LP | LP | LP | LP |

As saídas em C++ e em Python diferem apenas pela discretização da defuzzificação: em 205 pares de entrada (5 escolhidos à mão + 200 aleatórios uniformes em [−1, 1]²), a diferença absoluta máxima foi 6,4·10⁻⁴ e a média, 1,2·10⁻⁴.

## API HTTP

### `POST /control`

Requisição (JSON):

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| `error` | número | não (0) | Erro normalizado e. |
| `delta_error` | número | não (0) | Variação normalizada do erro ed. |
| `client_id` | texto | não | Identificador da planta, registrado como `IIoT_name`. |
| `seq` | inteiro | não (−1) | Índice da requisição na planta, registrado como `seq`. |

Resposta `200` (JSON): `{"u": <saída do FIS>, "pod": "<nome do pod>"}`. `u` é sempre a primeira chave, para que binários de planta que leem a resposta com `sscanf("{\"u\": %lf}")` continuem funcionando. JSON inválido ou entradas não numéricas → `400` com `{"error": "Invalid JSON or Processing Error"}` (não registrado no log).

### `GET /health`

`{"status": "ok", "pod": "<nome do pod>"}`. Usado como readiness probe do Kubernetes.

## Modelo de concorrência

- **C++**: a cpp-httplib atende as conexões com um pool de threads (padrão `max(8, hardware_concurrency − 1)` threads, crescendo sob demanda até 4× esse valor; pode ser fixado com `HTTP_THREADS`). Dentro de um contêiner, `hardware_concurrency` informa as CPUs do nó, e não o limite de CPU do pod. O motor do FuzzyLite não é thread-safe: ele guarda as entradas, a ativação das regras e o conjunto agregado da saída nos próprios objetos, então duas inferências simultâneas misturariam os dados ou corromperiam estruturas internas. Por isso a inferência é serializada por um mutex por processo: **um pod faz uma inferência por vez**, e a vazão só escala com mais réplicas. A leitura do JSON e a montagem da resposta acontecem fora do mutex. Outros padrões da cpp-httplib relevantes sob carga: backlog de `listen` igual a 5 (`CPPHTTPLIB_LISTEN_BACKLOG`), timeout de keep-alive de 5 s e no máximo 100 requisições por conexão.
- **Python**: servidor embutido do Flask (Werkzeug), uma thread por requisição; o `ControlSystemSimulation` do scikit-fuzzy tem o mesmo problema e é serializado com um `threading.Lock`. Esse servidor é voltado para desenvolvimento, e a inferência do scikit-fuzzy leva dezenas de milissegundos, então esta implementação não serve para malhas de 10 ms.

## Log por requisição

Cada réplica grava um arquivo CSV com uma linha por requisição `/control` (esquema em [data_schema.md](data_schema.md#log-do-controlador-result_csscsv)). Pontos de medição dentro do handler:

```
entrada do handler ─── lê JSON ─── espera o mutex da FIM ─── inferência ─── (resposta, linha de log)
     │                    │                  │                     │
  t_entry             t_parsed           t_locked               t_done
                          └─ LockWait_us ───┘└─ Inference_us ─────┘
  └──────────────────────────────── Handler_us ───────────────────┘
```

`Request_rate` é o número de requisições que obtiveram o mutex neste pod durante os últimos `RATE_WINDOW_MS` (janela deslizante, incluindo a atual), dividido pela duração da janela: requisições por segundo por pod.

O handler só acrescenta uma linha já formatada a um buffer em memória; uma thread em segundo plano grava o buffer em disco uma vez por segundo, mantendo a E/S de disco fora do caminho da requisição. Ao receber SIGINT/SIGTERM, o servidor para de aceitar conexões, conclui as requisições em andamento e grava as linhas restantes antes de sair.

## Configuração (variáveis de ambiente)

| Variável | Padrão | Descrição |
|----------|--------|-----------|
| `PORT` | `5000` | Porta de escuta. |
| `POD_NAME` | `$HOSTNAME` / hostname | Nome da réplica, gravado no log e devolvido na resposta. |
| `NODE_NAME` | `-` | Nome do nó (só no banner de inicialização). |
| `CONTROLLER_LOG` | `1` | `0` desativa o log CSV. |
| `LOG_DIR` | `.` | Diretório do log CSV. |
| `EXP_NAME` | vazio | Marcador opcional inserido no nome do arquivo. |
| `HTTP_THREADS` | padrão da cpp-httplib | Número fixo de threads de atendimento (só C++). |
| `RATE_WINDOW_MS` | `1000` | Janela do `Request_rate`. |

Nome do arquivo de log: `Result_CSS_<AAAA-MM-DD_HH-MM-SS>[_Exp_<EXP_NAME>]_pod_<POD_NAME>.csv`, onde a data é o instante de início do processo, em UTC. Um pod grava um único arquivo durante toda a sua vida; as linhas de rodadas diferentes do experimento são separadas pelo `IIoT_name` (que, com o `CLIENT_ID` padrão das plantas, contém a planta, M, K e o índice do cliente) e pelo tempo.

## Compilação

```bash
make ctrl_cpp                   # compilação nativa em controllers/fuzzy_cpp/build
make docker_cpp CPP_TAG=v3      # imagem de contêiner (Alpine 3.19, multi-stage)
make setup_py                   # virtualenv do controlador Python
```
