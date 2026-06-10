# A* Paralelo — Bidirectional A*

Implementação paralela do algoritmo A* utilizando a abordagem **Bidirectional A\*** com **OpenMP**, desenvolvida para navegação em grids 2D de grande escala.

---

## Visão geral

O algoritmo A* tradicional realiza a busca a partir do nó de início em direção ao destino. Esta implementação paraleliza a busca dividindo-a em **duas frentes simultâneas**:

- **Thread 1 (Forward)** — expande nós a partir do `start` em direção ao `end`
- **Thread 2 (Backward)** — expande nós a partir do `end` em direção ao `start`

Quando um nó é fechado por uma frente e está na closed list da outra, um caminho candidato é encontrado com custo `g_fwd(u) + g_bwd(u)`. O algoritmo encerra quando ambas as open lists possuem `min_f >= bestCost`, garantindo que o caminho encontrado é ótimo.

```
start ──► ──► ──►
                  ◄── ◄── ◄── end
              ▲
          meeting point
```

---

## Por que Bidirectional e não paralelizar vizinhos?

A abordagem inicial de paralelizar os 8 vizinhos de cada nó foi descartada por duas razões fundamentais:

| Abordagem | Problema |
|---|---|
| Paralelizar 8 vizinhos | Overhead de criar threads > tempo de checar 8 posições (~nanosegundos) |
| Paralelizar 8 vizinhos | A* é sequencial por natureza: cada iteração depende do resultado da anterior |
| **Bidirectional A\*** | Duas buscas independentes, granularidade real de paralelismo |
| **Bidirectional A\*** | Reduz ~50% dos nós expandidos em relação ao A* unidirecional |

---

## Requisitos

| Componente | Versão mínima |
|---|---|
| Compilador C++ | GCC 9+ / MinGW-w64 (MSYS2) |
| Padrão C++ | C++17 |
| OpenMP | 4.0+ |

> **Atenção:** MinGW 6.3.0 (32-bit) **não é suportado**. Use o MinGW-w64 instalado via MSYS2.

### Instalação do MinGW-w64 (Windows)

```bash
# 1. Instale o MSYS2: https://www.msys2.org
# 2. No terminal MSYS2, execute:
pacman -Syu
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make

# 3. Adicione ao PATH do Windows:
#    C:\msys64\mingw64\bin
```

---

## Compilação

```bash
g++ astar_bidirectional.cpp -o astar.exe -fopenmp -O2 -std=c++17
```

| Flag | Descrição |
|---|---|
| `-fopenmp` | Ativa suporte a OpenMP — **obrigatório**, sem ela as threads não são criadas |
| `-O2` | Otimizações seguras: inlining, vetorização, eliminação de código morto |
| `-std=c++17` | Padrão C++17 exigido pelo código |

---

## Configuração

Edite as constantes no topo do arquivo antes de compilar:

```cpp
std::string basePath = "gridsTeste/";        // pasta com os grids
std::vector<int> sizes     = {5000, 6000, 7000};   // tamanhos dos grids
std::vector<int> densities = {0, 10, 20, 30, 40, 50}; // densidades (%)
const int num_trials = 3;                    // repetições por grid
```

### Formato dos grids

Os grids são arquivos binários gerados pelo `grid_generator`. O nome segue o padrão:

```
gridsTeste/5000x5000_30.bin
```

Estrutura do arquivo:
```
[int gridX][int gridY][char cell_0_0][char cell_0_1]...[char cell_N_N]
```
onde `0 = livre` e `1 = obstáculo`.

---

## Execução

```bash
./astar.exe
```

Os resultados são salvos automaticamente em:

```
results/parallel_bidirectional.csv
```

---

## Saída — CSV de resultados

| Coluna | Descrição |
|---|---|
| `Teste` | Número sequencial do teste |
| `Tamanho do Grid` | Ex: `5000x5000_30` |
| `Percentual Obstaculos (%)` | Densidade de obstáculos |
| `Tempo de Execucao (s)` | Tempo em segundos com 9 casas decimais |
| `Nos Expandidos` | Total de nós expandidos pelas duas frentes |
| `Vizinhos Avaliados` | Total de vizinhos verificados |
| `Vizinhos Inseridos na Lista Aberta` | Total de inserções na open list |
| `Tamanho Maximo da Lista Aberta` | Pico combinado das duas open lists |
| `Comprimento do Caminho (passos)` | Número de nós no caminho (incluindo start e end) |
| `Custo Total do Caminho` | Soma dos custos de movimento (10 = cardinal, 14 = diagonal) |

---

## Detalhes técnicos

### Heurística — Octile Distance

```cpp
int dx = abs(x1 - x2), dy = abs(y1 - y2);
return 10 * (dx + dy) - 6 * std::min(dx, dy);
```

Admissível e consistente para movimento 8-direcional com custos `10` (cardinal) e `14` (diagonal). A heurística Manhattan foi descartada por ser inadmissível nesse modelo de movimento — ela superestima o custo diagonal e quebra a garantia de caminho ótimo do A*.

### Movimentos — 8 direções

```
↖ ↑ ↗
← · →     custo cardinal  = 10
↙ ↓ ↘     custo diagonal  = 14
```

**Corner cutting desativado:** movimentos diagonais são bloqueados quando qualquer um dos dois vizinhos cardinais adjacentes é obstáculo. Isso evita que o agente "espreme" pelo canto entre dois muros.

### Estrutura de dados — array flat 1D

Todas as estruturas internas (`grid`, `closedList`, `allNodes`) usam `vector<T>` flat com indexação `flat(x, y) = x * W + y`, em vez de `vector<vector<T>>`. Isso elimina os cache misses causados por linhas não contíguas na memória — crítico para grids de 5000×5000 ou maiores.

### Sincronização entre threads

| Estrutura | Mecanismo | Justificativa |
|---|---|---|
| Closed lists (`cFwd`, `cBwd`) | `atomic<bool>` com `release`/`acquire` | Leitura lock-free entre threads; garante visibilidade do valor de `g` sem lock |
| `bestCost` | `atomic<int>` + CAS (`compare_exchange_weak`) | Atualização lock-free do melhor custo encontrado |
| `meetX` / `meetY` | `omp_lock_t` | Seção raramente executada; lock simples sem impacto relevante |
| `searchDone`, `topFFwd`, `topFBwd` | `atomic<int>` com `relaxed` | Usados apenas na condição de término; consistência eventual é suficiente |

### Condição de término

O algoritmo encerra quando:

```
bestCost < INT_MAX  AND  topFFwd >= bestCost  AND  topFBwd >= bestCost
```

Isso garante que nenhuma expansão futura de nenhuma das duas frentes pode melhorar o caminho já encontrado — a solução é ótima.

### Reconstrução do caminho

O caminho final é composto por duas partes:

```
start ──► [pathFwd] ──► meetingPoint ──► [pathBwd] ──► end
```

- `pathFwd`: reconstruído seguindo os `parent_x/y` de `nFwd` a partir do meeting point até o start, depois invertido
- `pathBwd`: reconstruído seguindo os `parent_x/y` de `nBwd` a partir do parent do meeting point até o end

---

## Estrutura do projeto

```
.
├── astar_bidirectional.cpp   # algoritmo paralelo (este arquivo)
├── astar_serial.cpp          # algoritmo serial para comparação
├── grid_generator.cpp        # gerador de grids binários
├── gridsTeste/               # grids de entrada
│   ├── 5000x5000_0.bin
│   ├── 5000x5000_10.bin
│   └── ...
└── results/
    ├── parallel_bidirectional.csv
    └── serial.csv
```

---

## Comparação com a versão serial

| Característica | Serial | Bidirectional Paralelo |
|---|---|---|
| Threads | 1 | 2 |
| Direções de busca | 1 (start → end) | 2 (start → end e end → start) |
| Nós expandidos (estimado) | N | ~N/2 |
| Heurística | Octile | Octile |
| Corner cutting | Desativado | Desativado |
| Estrutura de memória | Array flat 1D | Array flat 1D |
| Métricas | `struct Metrics` | `struct Metrics` (por frente, agregadas) |