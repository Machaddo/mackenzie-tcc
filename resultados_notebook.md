
# Análise Comparativa de Algoritmos A* Paralelos
### Estudo sobre Paralelização do Algoritmo A* em Arquiteturas de Memória Compartilhada
**Universidade Presbiteriana Mackenzie — Faculdade de Computação e Informática**

---

## Visão Geral

Este repositório contém o notebook Python e os dados experimentais do artigo **"Estudo sobre a Paralelização do Algoritmo A* em Arquiteturas de Memória Compartilhada"**, desenvolvido como Trabalho de Conclusão de Curso (TCC II) na FCI Mackenzie.

O objetivo central é comparar o desempenho de quatro abordagens do algoritmo A* em grids bidimensionais de grande escala:

| Abordagem | Tipo | Threads |
|---|---|---|
| **Serial** | Sequencial (referência) | 1 |
| **Bidirecional A\*** | Paralela — duas frentes de busca | 2 |
| **HDA\*** | Paralela — hashing distribuído | 4 |
| **PBNF** | Paralela — particionamento geográfico | 4 |

---

## Estrutura do Repositório

```
.
├── astar_analysis.ipynb          # Notebook principal de análise
├── serial.csv                    # Resultados do A* serial
├── parallel_bidirectional.csv    # Resultados do Bidirecional A*
├── parallel_hda.csv              # Resultados do HDA*
├── parallel_pbnf.csv             # Resultados do PBNF
└── README.md                     # Este arquivo
```

---

## Pré-requisitos

- Python **3.8+**
- Jupyter Notebook ou JupyterLab

### Instalação das dependências

```bash
pip install pandas numpy matplotlib seaborn notebook
```

---

## Como Executar

1. **Clone ou baixe** este repositório
2. Certifique-se de que os quatro arquivos `.csv` estão na **mesma pasta** do notebook
3. Abra o Jupyter:

```bash
jupyter notebook astar_analysis.ipynb
```

4. Execute todas as células em ordem: menu **Kernel → Restart & Run All**

> Cada gráfico gerado é automaticamente salvo como `.png` na mesma pasta do notebook.

---

## Dados Experimentais

### Configurações Testadas

| Variável | Valores |
|---|---|
| Tamanho do grid | 5.000×5.000, 6.000×6.000, 7.000×7.000 células |
| Densidade de obstáculos | 0%, 10%, 20%, 30% |
| Execuções por configuração | 20 execuções independentes |
| **Total de registros** | **960 execuções** |

### Métricas Coletadas por Execução

| Coluna no CSV | Descrição |
|---|---|
| `Tempo de Execucao (ms)` | Tempo total da busca em milissegundos |
| `Nos Expandidos` | Quantidade de nós retirados da lista aberta e processados |
| `Vizinhos Avaliados` | Total de nós vizinhos inspecionados |
| `Vizinhos Inseridos na Lista Aberta` | Nós efetivamente enfileirados |
| `Tamanho Maximo da Lista Aberta` | Pico de ocupação da fila de prioridade |
| `Comprimento do Caminho (passos)` | Número de movimentos do caminho encontrado |
| `Custo Total do Caminho` | Soma dos pesos dos movimentos (ortogonal=10, diagonal=14) |

### Ambiente de Execução dos Experimentos

| Componente | Especificação |
|---|---|
| Processador | Intel Core i5-12450H (4 Performance Cores, 4 Efficiency Cores, 12 Threads) |
| Memória RAM | 16 GB RAM DDR4 1600 MHz |
| Sistema Operacional | Windows 11 |
| Linguagem / Compilador | C++, GCC em MinGW-w64 (-03) |
| Biblioteca paralela | OpenMP 4.5  |

---

## Seções do Notebook

### Seção 1 — Carregamento e Preparação dos Dados
Instala os pacotes necessários e define as constantes globais do projeto: caminhos dos CSVs, cores, marcadores, número de threads por abordagem e parâmetros visuais dos gráficos.
Lê os quatro CSVs, padroniza os tipos, extrai o tamanho do grid e calcula as estatísticas agregadas para cada combinação de abordagem, grid e densidade de obstáculos:
> **Ponto de atenção:** se os CSVs estiverem em outra pasta, altere as variáveis `CSV_SERIAL`, `CSV_BIDIRECIONAL`, `CSV_HDA` e `CSV_PBNF` nesta seção antes de executar o restante.
> 
- **Média e desvio padrão** de tempo, nós expandidos e custo
- **Speedup** = T_serial / T_paralela (por grid e obstáculo)
- **Eficiência** = Speedup / Nº de threads

---

### Seção 2 — Tabela de Tempo Médio (Tabela 3 do artigo)
Reproduz a tabela original do artigo com os tempos médios de execução em milissegundos e seus desvios padrão para o grid 5K×5K, separados por percentual de obstáculos.

**O que observar:** o desvio padrão alto do HDA\* e PBNF com obstáculos indica instabilidade de desempenho, enquanto o Bidirecional mantém desvios entre 27–50 ms em todos os cenários.

---

### Seção 3 — Figura 1: Tempo de Execução Serial vs Paralelos
Gráfico de linhas em **escala logarítmica** comparando as quatro abordagens no grid 5K×5K. A escala log é necessária porque as diferenças chegam a três ordens de grandeza entre o Bidirecional e o PBNF com 30% de obstáculos.

**O que observar:** o cruzamento entre o Serial e o Bidirecional entre 0% e 10% de obstáculos — ponto a partir do qual a busca bidirecional passa a ser consistentemente mais rápida.

---

### Seção 4 — Tabela de Speedup (Tabela 4 do artigo)
Tabela com os valores de speedup de cada abordagem paralela em relação ao Serial para o grid 5K×5K. Valores acima de 1 indicam ganho; abaixo de 1, perda de desempenho.

**Destaque:** o Bidirecional A\* atinge speedup máximo de **5,61×** com 20% de obstáculos; o PBNF despenca para **0,17×** com 30%, tornando-se quase 6× mais lento que o Serial.

---

### Seção 5 — Figura 2: Speedup das Abordagens Paralelas
Gráfico de linhas com anotações nos pontos do Bidirecional A\*, mostrando a evolução do speedup conforme os obstáculos aumentam. A linha tracejada marca o ponto de equilíbrio (speedup = 1).

**O que observar:** o comportamento oposto entre o Bidirecional (speedup crescente com obstáculos) e PBNF/HDA\* (speedup decrescente). O cruzamento da linha de referência ilustra onde cada abordagem passa a ser prejudicial.

---

### Seção 6 — Figura 3: Eficiência das Abordagens Paralelas
Gráfico de eficiência normalizada pelo número de threads (E = Speedup / p). Uma eficiência acima de 1,0 caracteriza comportamento **superlinear** — cada thread entrega mais do que entregaria isoladamente.

**O que observar:** o Bidirecional A\* atinge eficiência de **2,81** com 20% de obstáculos e apenas 2 threads. Isso ocorre porque as duas frentes de busca reduzem mutuamente o espaço a ser explorado, gerando ganho algorítmico além do simples paralelismo.

---

### Seção 7 — Figuras 4, 5, 6: Nós Expandidos por Abordagem
Gráfico de barras agrupadas em **escala logarítmica** para os três tamanhos de grid. Mostra o esforço de exploração de cada algoritmo, ou seja, quantos nós precisaram ser processados para encontrar o caminho.

**O que observar:** sem obstáculos, Serial e Bidirecional expandem a menor quantidade de nós; com obstáculos, o Serial passa a expandir ordens de grandeza a mais. O PBNF, apesar de lento em tempo, consegue conter o número de nós expandidos em ambientes com obstáculos — indicando que o gargalo está na coordenação entre threads, não na exploração.

---

### Seção 8 — Figuras 7, 8, 9: Custo Total do Caminho
Gráfico de barras comparando o custo médio do caminho encontrado por cada abordagem nos três grids. O custo é a soma dos pesos dos movimentos (10 por passo ortogonal, 14 por diagonal).

**O que observar:** em sua grande maioria, todas as abordagens encontram caminhos com custos muito próximos — confirmando que paralelização não prejudica a qualidade da solução. O HDA\* apresenta leve degradação em cenários mais densos. O Serial tende a apresentar custo ligeiramente maior com obstáculos, pois percorre mais o espaço antes de convergir.

---

### Seção 9 — Figura 10: Comparação Direta Entre Paralelas
Gráfico de barras em escala logarítmica excluindo o Serial, para isolar as diferenças entre as três abordagens paralelas.

**O que observar:** sem obstáculos, o PBNF lidera com 298,8 ms. A partir de 10%, o Bidirecional passa a dominar progressivamente. Com 30%, o PBNF e o HDA\* são respectivamente **30×** e **27×** mais lentos que o Bidirecional.

---

### Seção 10 — Figura 11: Bidirecional A\* vs Serial por Tamanho de Grid
Gráfico de linhas com duas séries por grid (linha sólida = Bidirecional, linha tracejada = Serial), permitindo comparar visualmente como a diferença entre as abordagens cresce com o tamanho do problema.

**O que observar:** quanto maior o grid, mais visível e expressiva se torna a vantagem do Bidirecional. No grid 7K×7K com 20% de obstáculos, o Serial leva mais de 4,6× mais tempo.

---

### Seção 11 — Figura 12: Speedup do Bidirecional por Tamanho de Grid
Evolução do speedup do Bidirecional A\* nos três grids sobrepostos em um único gráfico, com anotações em cada ponto.

**O que observar:** o speedup é consistente entre os tamanhos (pico em 20% para 5K e 6K). No grid 7K, o speedup cai de 4,60× para 3,42× entre 20% e 30%, possivelmente pela maior variabilidade dos grids nessa densidade, que dificulta o encontro eficiente das duas frentes.

---

### Seção 12 — Tabela 5: Escalabilidade Completa
Tabela consolidada com todos os tempos médios e speedups para as combinações de grid e densidade de obstáculos. Serve como referência numérica completa para os gráficos de escalabilidade.

---

### Seção 13 — Tabela 6: Síntese Comparativa
Tabela qualitativa resumindo o comportamento de cada abordagem nos critérios: desempenho sem/com obstáculos, escalabilidade, garantia de otimalidade, overhead paralelo e recomendação de uso.

---

### Seção 14 — Bônus: Painel Completo de Speedup
Painel com três subgráficos lado a lado (um por tamanho de grid) mostrando o speedup das três abordagens paralelas simultaneamente. Permite comparar visualmente como o comportamento muda conforme o problema escala.

---

### Seção 15 — Bônus: Boxplot de Variabilidade
Boxplot das 20 execuções individuais por abordagem e por densidade de obstáculos, em escala logarítmica. Revela a dispersão real dos dados além da média — fundamental para avaliar a consistência de cada algoritmo.

**O que observar:** o Bidirecional A\* apresenta caixas compactas em todos os cenários; o HDA\* com 0% de obstáculos mostra outliers extremos, reflexo de execuções onde o overhead de hashing dominou completamente o tempo.

---

## Resultados Principais

| Conclusão | Evidência |
|---|---|
| O Bidirecional A\* é a única abordagem com ganho real sobre o Serial com obstáculos | Speedup de até **5,61×** no grid 5K×5K com 20% de obstrução |
| O PBNF só é competitivo em ambientes sem obstáculos | Speedup de **1,79×** sem obstáculos; colapsa para **0,17×** com 30% |
| Mais threads não garante melhor desempenho | HDA\* e PBNF com 4 threads são sistematicamente piores que o Serial com 1 thread |
| A qualidade do caminho é preservada pela paralelização | Custos totais equivalentes entre todas as abordagens na maioria dos cenários |
| O Bidirecional apresenta eficiência superlinear | Eficiência de **2,81** com apenas 2 threads — ganho algorítmico além do paralelismo |

---

## Referências

- BURNS et al. Best-first heuristic search for multicore machines. *JAIR*, v. 39, 2010.
- KISHIMOTO; FUKUNAGA; BOTEA. Evaluation of a simple, scalable, parallel best-first search strategy. *Artificial Intelligence*, v. 195, 2013.
- HOLTE et al. Bidirectional search that is guaranteed to meet in the middle. *AAAI*, 2016.
- RUSSELL; NORVIG. *Inteligência Artificial*. 3. ed. LTC, 2021.
- PACHECO. *An Introduction to Parallel Programming*. Morgan Kaufmann, 2011.

---

*Universidade Presbiteriana Mackenzie — Faculdade de Computação e Informática — 2026*
