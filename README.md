# Mackenzie TCC - Ciência da Computação

## Estudo sobre a Paralelização do Algoritmo A* em Arquiteturas de Memória Compartilhada

**Orientador:** Prof. Dr. Jean Marcos Laine  
**Alunos:** Vitor Machado & Rodrigo Lucas  

---

### Sobre o TCC
O algoritmo A* é um dos métodos de busca mais amplamente utilizados na
computação, especialmente em aplicações que envolvem planejamento de trajetórias e
navegação em ambientes discretos. Sua eficiência depende diretamente da qualidade da
heurística utilizada e das características do espaço de busca, tornando-o uma referência
consolidada para problemas de pathfinding (RUSSELL; NORVIG, 2021). Em contextos de
alta complexidade, como grids de grande escala e com múltiplos obstáculos, o tempo de
execução do A* serial pode se tornar proibitivo, motivando a investigação de abordagens
paralelas. 

O que fizemos aqui foi colocar esse algoritmo no limite: rodamos ele em mapas (grids) bidimensionais de larga escala e exploramos diferentes formas de **paralelizar o seu processamento** em arquiteturas de memória compartilhada.

Analisamos a fundo os ganhos reais e os gargalos do paralelismo, medindo o tempo de execução, o *speedup*, a eficiência e o esforço computacional (nós expandidos) em cenários variando tanto o tamanho do grid quanto a quantidade de obstáculos.

---

### Algoritmos Avaliados
A partir da versão serial do A* fizemos uma análise comparativa com 3 abordagens diferentes de paralelização. 

* **Serial (Sequencial):** O baseline clássico rodando em uma única thread.
* **Bidirecional A\*:** Busca paralela partindo simultaneamente da origem e do destino.
* **HDA\* (Hash Distributed A\*):** Distribuição do espaço de estados entre as threads usando funções de hash.
* **PBNF (Parallel Best-Niche-First):** Abordagem focada em particionar e explorar as áreas (blocos) mais promissoras do grafo em paralelo.

---

### Explorar o TCC
Para explorar nosso TCC e os resultados que alcançamos, comece pelo readme [resultados_notebook.md](./resultados_notebook.md) ele oferece uma breve explicação de como explorar o repositório 
e compreender os resultados que obtivemos. 
Para entender as fases de teste e validação, acesse o readme [testes.md](./testes.md)
