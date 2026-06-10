# 🧪 Cenários de Teste — Algoritmo A*

Documentação dos cenários de teste realizados para validação do algoritmo de busca A*, cobrindo ambientes sem obstáculos, com obstáculos e análise de heurísticas. Todos os testes foram executados em uma única máquina.

---

## 📋 Índice

1. [Ambiente de Teste](#1-ambiente-de-teste)
2. [Metodologia](#2-metodologia)
3. [Modelo de Movimento](#3-modelo-de-movimento)
4. [Cenário 1 — Caminhos Sem Obstáculos](#4-cenário-1--caminhos-sem-obstáculos)
   - [Grid 5x5](#41-grid-5x5)
   - [Grid 10x10](#42-grid-10x10)
   - [Demais Grids](#43-demais-grids)
5. [Cenário 2 — Caminhos Com Obstáculos](#5-cenário-2--caminhos-com-obstáculos)
   - [Problema de Geração de Grids](#51-problema-de-geração-de-grids)
   - [Abordagens Consideradas](#52-abordagens-consideradas)
   - [Abordagem Escolhida](#53-abordagem-escolhida)
   - [Limiar de Percolação](#54-limiar-de-percolação)
6. [Cenário 3 — Heurística Inadmissível (Manhattan)](#6-cenário-3--heurística-inadmissível-manhattan)
7. [Cenário 4 — Corner Cutting](#7-cenário-4--corner-cutting)
9. [Conclusões Gerais](#9-conclusões-gerais)

---

## 1. Ambiente de Teste

Os testes foram executados em uma única máquina com as seguintes especificações:

| Componente | Detalhe |
|------------|---------|
| **Nome da máquina** | MCAIT04 |
| **Processador** | 12th Gen Intel® Core™ i5-12450H |
| **Arquitetura** | x86-64 (64-bit) |
| **Clock base** | 2000 MHz |
| **Núcleos físicos** | 8 |
| **Núcleos lógicos (threads)** | 12 |
| **Cache L2** | 7168 KB (7 MB) |
| **Cache L3** | 12288 KB (12 MB) |
| **Fabricante** | GenuineIntel |

---

## 2. Metodologia

O objetivo inicial foi **validar o comportamento correto do algoritmo A\*** antes de avançar para cenários mais complexos.

**Protocolo adotado:**

1. Implementar 7 diferentes grids de teste.
2. Executar cada grid **15 vezes** por configuração (tamanho × percentual de obstáculos).
3. Reinicializar todas as métricas a cada execução individual, garantindo amostras independentes.
4. Coletar métricas de desempenho e estrutura interna do algoritmo.

**Métricas coletadas:**

- Tempo de execução (segundos)
- Nós expandidos
- Vizinhos avaliados
- Vizinhos inseridos na lista aberta
- Tamanho máximo da lista aberta
- Comprimento do caminho (passos)
- Custo total do caminho

---

## 3. Modelo de Movimento

| Tipo de Movimento | Custo |
|-------------------|-------|
| Ortogonal (cima, baixo, esquerda, direita) | `10` |
| Diagonal | `14` |

> O custo diagonal de 14 aproxima a distância euclidiana real (√2 ≈ 1,414) sem utilizar ponto flutuante, mantendo os cálculos em aritmética inteira.

O modelo permite deslocamento em **8 direções**.

---

## 4. Cenário 1 — Caminhos Sem Obstáculos

**Objetivo:** Verificar se o A* encontra sempre o mesmo caminho ótimo em ambientes sem obstáculos, confirmando consistência da heurística.

### 4.1 Grid 5x5

**Configuração:** 5×5, 0% de obstáculos, 5 execuções registradas.

**Passo a passo:**
1. Gerar grid 5×5 sem obstáculos.
2. Definir ponto de origem (A) e destino (B).
3. Executar o A* e registrar métricas.
4. Repetir 15 vezes.

**Resultados — Tempo de Execução:**

| Teste | Tamanho do Grid | Obstáculos (%) | Tempo de Execução (s) | Nós Expandidos |
|-------|----------------|----------------|-----------------------|----------------|
| 1 | 5x5_0 | 0 | 0.000000000 | 5 |
| 2 | 5x5_0 | 0 | 0.000000000 | 5 |
| 3 | 5x5_0 | 0 | 0.000000000 | 5 |
| 4 | 5x5_0 | 0 | 0.000000000 | 5 |
| 5 | 5x5_0 | 0 | 0.000000000 | 5 |

**Resultados — Métricas de Granularidade:**

| Vizinhos Avaliados | Inseridos na Lista Aberta | Tamanho Máx. Lista Aberta | Comprimento (passos) | Custo Total |
|--------------------|--------------------------|---------------------------|----------------------|-------------|
| 32 | 18 | 14 | 4 | 56 |
| 32 | 18 | 14 | 4 | 56 |
| 32 | 18 | 14 | 4 | 56 |
| 32 | 18 | 14 | 4 | 56 |
| 32 | 18 | 14 | 4 | 56 |

**Conclusão:** O tempo de execução é inferior à resolução mínima do clock do sistema para instâncias pequenas, resultando em medições nulas. Todos os testes retornam o mesmo comprimento, custo e métricas, confirmando que a heurística é **admissível e consistente**.

---

### 4.2 Grid 10x10

**Configuração:** 10×10, 0% de obstáculos.

**Resultados — Tempo de Execução:**

| Teste | Tamanho do Grid | Obstáculos (%) | Tempo de Execução (s) | Nós Expandidos |
|-------|----------------|----------------|-----------------------|----------------|
| 1 | 10x10_0 | 0 | 0.000000000 | 10 |
| 2 | 10x10_0 | 0 | 0.000000000 | 10 |
| 3 | 10x10_0 | 0 | 0.000000000 | 10 |
| 4 | 10x10_0 | 0 | 0.000000000 | 10 |
| 5 | 10x10_0 | 0 | 0.001128000 | 10 |

**Resultados — Métricas de Granularidade:**

| Vizinhos Avaliados | Inseridos na Lista Aberta | Tamanho Máx. Lista Aberta | Comprimento (passos) | Custo Total |
|--------------------|--------------------------|---------------------------|----------------------|-------------|
| 43 | 34 | 9 | 9 | 126 |
| 43 | 34 | 9 | 9 | 126 |
| 43 | 34 | 9 | 9 | 126 |
| 43 | 34 | 9 | 9 | 126 |
| 43 | 34 | 9 | 9 | 126 |

**Conclusão:** O algoritmo mantém consistência. O **teste 5** apresentou tempo mensurável (0.001128s), evidenciando que a medição de tempo não é contínua nem exata em escalas de nanossegundos — variações de sistema operacional e scheduler podem introduzir ruído pontual.

---

### 4.3 Demais Grids

**Configuração:** Grids de 15×15 a 50×50, 0% de obstáculos.

**Resultados — Tempo de Execução:**

| Teste | Tamanho do Grid | Obstáculos (%) | Tempo de Execução (s) | Nós Expandidos |
|-------|----------------|----------------|-----------------------|----------------|
| 1 | 15x15_0 | 0 | 0.000000000 | 15 |
| 2 | 20x20_0 | 0 | 0.000000000 | 20 |
| 3 | 30x30_0 | 0 | 0.000000000 | 30 |
| 4 | 50x50_0 | 0 | 0.000999000 | 50 |

**Resultados — Métricas de Granularidade:**

| Vizinhos Avaliados | Inseridos na Lista Aberta | Tamanho Máx. Lista Aberta | Comprimento (passos) | Custo Total |
|--------------------|--------------------------|---------------------------|----------------------|-------------|
| 112 | 68 | 54 | 14 | 196 |
| 152 | 93 | 74 | 19 | 266 |
| 232 | 143 | 114 | 29 | 406 |
| 392 | 243 | 194 | 49 | 686 |

**Conclusão:** O tempo de execução mensurável surge a partir do grid 40×40/50×50, confirmando que o impacto no clock do sistema só aparece com instâncias maiores. As métricas crescem de forma linear e previsível com o tamanho do grid.

---

## 5. Cenário 2 — Caminhos Com Obstáculos

### 5.1 Problema de Geração de Grids

**Objetivo:** Avaliar o comportamento do A* em ambientes com obstáculos e compreender os limites de viabilidade de geração de grids.

**O problema:** Gerar um grid com alta densidade de obstáculos que ainda possua um caminho válido de A a B não é trivial.

**Análise por percentual em grid 5x5 (25 células):**

| Percentual | Células Bloqueadas | Viável? |
|------------|--------------------|---------|
| 90% | ~22–23 | ❌ Inviável |
| 80% | 20 | ⚠️ Apenas se a diagonal estiver livre |
| 70% | 17 | ✅ Possível |
| 60% | 15 | ✅ Possível |
| 50% | 12 | ✅ Possível |
| 40% | 10 | ✅ Possível |
| 30% | 7 | ✅ Possível |
| 20% | 5 | ✅ Possível |
| 10% | 2 | ✅ Possível |
| 0% | 0 | ✅ Trivial |

> **Observação:** Em qualquer grid, a diagonal sempre representa o caminho mais curto. Com 80%+ de obstáculos em grids pequenos, a diagonal tende a ser bloqueada, tornando o caminho impossível.

---

### 5.2 Abordagens Consideradas

Três abordagens foram avaliadas para geração de grids com obstáculos:

**Abordagem 1: Colocação Incremental com Verificação**
- Insere um obstáculo por vez e verifica conectividade após cada inserção.
- Se bloquear o caminho, descarta e tenta outro.
- ✅ Simples, sempre termina.
- ❌ Chama BFS a cada obstáculo — complexidade **O(N⁴)**, inviável para grids grandes.

**Abordagem 2: Garantir Caminho Primeiro, Depois Preencher**
- Gera um caminho garantido via caminhada aleatória, depois preenche o restante com obstáculos.
- ✅ Termina em **O(N²)**, sem loops.
- ❌ Cria um corredor forçado que **enviesa o benchmark** — o A* navega de forma artificial e previsível.

**Abordagem 3: Limite de Tentativas com Fallback**
- Tenta gerar aleatoriamente até N vezes. Se exceder o limite, reporta falha na criação.
- ✅ Preserva aleatoriedade, simples de implementar.
- ❌ Em densidades muito altas pode demorar mesmo com o fallback.
- **Complexidade: O(T × N²)** — onde T é o número máximo de tentativas e N² é o custo de preencher o grid e verificar conectividade via BFS/DFS a cada tentativa. Como T é uma constante configurada (ex: 100 ou 1000), simplifica-se para **O(N²)** no caso médio com baixas densidades. Em densidades próximas ao limiar de percolação, T tende ao máximo, tornando **O(T · N²)** o cenário mais representativo.

---

### 5.3 Abordagem Escolhida

> **Abordagem 3 — Limite de Tentativas com Fallback**

**Justificativa:** Permite simular um cenário mais realista para o algoritmo e aumenta o desafio real do A*, sem artificializar o caminho de teste. A aleatoriedade preservada aproxima o benchmark de situações reais de uso.

---

### 5.4 Limiar de Percolação

Em grids 2D com 8 direções, o **limiar de percolação** está em aproximadamente **41% de obstáculos**. Acima desse valor, a probabilidade de existir um caminho válido cai para praticamente zero, independentemente da semente ou número de tentativas.

| Densidade de Obstáculos | Situação |
|------------------------|----------|
| ≤ 40% | Abaixo do limiar → caminho existe ✅ |
| 50%–80% | Acima do limiar → caminho quase impossível ❌ |
| 90% | Overflow → grid vazio → caminho trivial ⚠️ |

**Conclusão:** Para densidades entre 50%–80% com grids grandes, nenhuma quantidade de tentativas resolve o problema — o caminho **matematicamente não existe** na grande maioria dos grids aleatórios com essa densidade. A única solução real nesses casos seria uma **Abordagem de Reparo Mínimo** (garantir conectividade mínima após geração).

---

## 6. Cenário 3 — Heurística Inadmissível (Manhattan)

**Objetivo:** Identificar o impacto do uso da distância de Manhattan como heurística em um algoritmo que suporta movimentos diagonais.

**O problema:**

A distância de Manhattan assume apenas movimentos ortogonais. Quando aplicada a um algoritmo com custo diagonal de 14, ela **superestima** o custo real dos caminhos diagonais.

**Exemplo:**
- Nó a 3 passos na diagonal:
  - Estimativa Manhattan: `(3 + 3) × 10 = 60`
  - Custo real octile: `3 × 14 = 42`
  - **Superestimativa de 18 unidades**

**Consequência:** Com uma heurística inadmissível, o A* pode encerrar a busca ao encontrar uma **primeira solução subótima** (em ziguezague ortogonal), sem garantir que é a melhor.

**Solução — Heurística Octile:**

```
h = 10 × (dx + dy) - 6 × min(dx, dy)
```

A heurística octile é matematicamente derivada dos custos reais (ortogonal=10, diagonal=14) e garante admissibilidade.

**Referência:** [Stanford — GameProgramming Heuristics](https://theory.stanford.edu/~amitp/GameProgramming/Heuristics.html)

**Conclusão:** A heurística de Manhattan é **inadmissível** para movimentos diagonais com os custos adotados. Deve ser substituída pela heurística octile para garantir o caminho ótimo.

---

## 7. Cenário 4 — Corner Cutting

**Objetivo:** Verificar o comportamento do algoritmo ao realizar movimentos diagonais em cantos entre dois obstáculos adjacentes (corner cutting).

**O problema:** O A* com movimentos diagonais pode "cortar cantos" entre dois obstáculos adjacentes, atravessando um espaço geometricamente inválido.

**Referência:** [GameDev StackExchange — Corner Cutting in A*](https://gamedev.stackexchange.com/questions/175783/how-to-fix-cutting-corner-problem-in-a-star-pathfinding)

**Conclusão:** O comportamento de corner cutting deve ser explicitamente tratado na lógica de vizinhos do algoritmo, invalidando movimentos diagonais quando ambos os lados ortogonais adjacentes estiverem bloqueados.
---

## 9. Conclusões Gerais

| Cenário | Resultado |
|---------|-----------|
| A* sem obstáculos | ✅ Heurística admissível e consistente confirmada — mesmo caminho, mesmo custo em todas as execuções |
| Grids com obstáculos | ✅ Abordagem 3 (tentativas com fallback) adotada; limiar de percolação ~41% define a viabilidade prática |
| Heurística Manhattan | ❌ Inadmissível para movimentos diagonais; substituir por heurística **octile** |
| Corner cutting | ⚠️ Deve ser tratado explicitamente na lógica de vizinhos |