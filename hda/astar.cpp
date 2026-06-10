// Implementação do Algoritmo HDA* (Hash-Distributed A*) com OpenMP
// =================================================================
//
// DESCRIÇÃO DO ALGORITMO:
// O HDA* é uma adaptação paralela do algoritmo de busca A*, projetada para 
// otimizar a exploração de espaços de estado em arquiteturas de memória compartilhada.
//
// MECANISMO DE PARALELIZAÇÃO:
// - Particionamento Dinâmico: O espaço de estados (grid) é particionado entre P threads 
//   através de uma função de hash aplicada às coordenadas dos nós.
// - Atribuição: Cada thread possui posse exclusiva de expansão sobre 
//   o seu subconjunto de nós, mitigando a necessidade de locks na memória global.
// - Comunicação: A propagação de atualizações de caminhos mais eficientes 
//   entre partições distintas é realizada no envio de mensagens para caixas 
//   de entrada (inboxes) locais de cada thread.

#include <iostream>
#include <vector>
#include <queue>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <utility>
#include <atomic>
#include <climits>
#include <memory>
#include <omp.h>

using namespace std;

// CONFIGURAÇÃO
int test_num = 1;
const int num_trials  = 20;
const int NUM_THREADS = 4; // Define o número de threads alocadas para a execução do OpenMP

string basePath = "gridsTeste/";
vector<int> sizes     = {6000,7000};
vector<int> densities = {0, 10, 20, 30};

// ESTRUTURAS DE DADOS

// Nó do grafo com custo g, heurística h e f = g + h
struct Node {
    int x, y;
    int f, g, h;
    int parent_x, parent_y;
};

// Função de comparação para a Fila de Prioridade (Min-Heap).
struct CompareNode {
    bool operator()(const Node* a, const Node* b) const { return a->f > b->f; }
};

// Coleta de estatísticas 
struct Metrics {
    long long expandedNodes    = 0;
    long long neighborsChecked = 0;
    long long neighborsAdded   = 0;
    long long maxOpenListSize  = 0;
    int pathLength = 0;
    int pathCost   = 0;
};

// Informações de grid para testes
struct GridInfo {
    string filename, name;
    int obstaclePercentage;
};

// COMUNICAÇÃO ENTRE THREADS (HDA*)
// Pacote de dados utilizado para propor um caminho de menor custo a um nó
struct Message {
    int x, y;              // Coordenada do nó
    int g;                 // Custo proposto
    int parent_x, parent_y; // Para reconstruir caminho
};

// CAIXA DE MENSAGENS (Inbox)
// Estrutura thread-safe responsável por gerenciar o recebimento assíncrono de mensagens.
struct Inbox {
    omp_lock_t      lock;
    vector<Message> msgs;

    Inbox()  { omp_init_lock(&lock); }
    ~Inbox() { omp_destroy_lock(&lock); }

    Inbox(const Inbox&)            = delete;
    Inbox& operator=(const Inbox&) = delete;
};

//  UTILITÁRIOS 
// Converte (x, y) para índice linear no array 1D (melhor cache)
inline int flat(int x, int y, int W) { return x * W + y; }

// Realiza a leitura estruturada do grid de obstáculos a partir de um arquivo binário.
// Carrega os dados preenchendo as dimensões e o vetor que representa o mapa.
bool loadGridFromFile(const string& fn,
                      vector<int>& grid,
                      int& gridWidth, int& gridHeight) {
    ifstream file(fn, ios::binary);
    if (!file) { cerr << "Erro ao abrir " << fn << "\n"; return false; }
    file.read(reinterpret_cast<char*>(&gridWidth), sizeof(int));
    file.read(reinterpret_cast<char*>(&gridHeight), sizeof(int));
    grid.resize(gridWidth * gridHeight);
    for (int i = 0; i < gridWidth; ++i)
        for (int j = 0; j < gridHeight; ++j) {
            char c; file.read(&c, sizeof(char));
            grid[flat(i, j, gridHeight)] = c;
        }
    return true;
}

// Verifica se coordenada está dentro do grid
bool isValid(int x, int y, int gridWidth, int gridHeight) {
    return x >= 0 && x < gridWidth && y >= 0 && y < gridHeight;
}

// Heurística octile: calcula distância aproximada (consistente, admissível)
// Fórmula: 10*(dx+dy) - 6*min(dx,dy)
// Apropriada para movimento 8-direcional (custos 10 e 14)
inline int octile(int x1, int y1, int x2, int y2) {
    int dx = abs(x1 - x2), dy = abs(y1 - y2);
    return 10 * (dx + dy) - 6 * min(dx, dy);
}

void initNodes(vector<Node>& nodes, int gridWidth, int gridHeight) {
    nodes.resize(gridWidth * gridHeight);
    for (int i = 0; i < gridWidth; ++i)
        for (int j = 0; j < gridHeight; ++j) {
            auto& n = nodes[flat(i, j, gridHeight)];
            n.x = i; n.y = j;
            n.f = n.g = n.h = INT_MAX / 2;  // Inicializa com valor grande
            n.parent_x = n.parent_y = -1;
        }
}

const int DX[] = {-1, -1, -1,  0,  0,  1,  1,  1};
const int DY[] = {-1,  0,  1, -1,  1, -1,  0,  1};

// ===== HDA* (Hash-Distributed A*) =====
// 
// PRINCÍPIO CENTRAL:
// - Particiona o grafo entre P threads usando hash(x, y) → cada nó tem um "dono"
// - Cada thread expande apenas os nós que possui
// - Comunicação entre threads via caixas de mensagens (inboxes)
// - Sem locks globais: contenção apenas nas inboxes individuais
//
// VANTAGENS:
// - Evita race conditions: nodes[i] escrito apenas pelo dono
// - Escalável: cada thread tem sua própria open list
// - Distribuição automática: hash balanceia carga entre threads

// Define qual thread é "dono" de um nó (x, y)
// Usa hash para distribuir nós uniformemente entre threads
inline int stateOwner(int x, int y, int P) {
    unsigned h = ((unsigned)x * 2654435761u) ^ ((unsigned)y * 2246822519u);
    return (int)(h % (unsigned)P);
}

vector<pair<int,int>> parallelDistributedAStarSearch(
    const vector<int>& grid,
    int startX, int startY,
    int endX, int endY,
    int gridWidth, int gridHeight,
    Metrics& metrics,
    int numThreads = NUM_THREADS)
{
    auto isObs = [&](int x, int y) { return grid[flat(x,y,gridHeight)] == 1; };

    if (!isValid(startX,startY,gridWidth,gridHeight) || !isValid(endX,endY,gridWidth,gridHeight) ||
        isObs(startX,startY) || isObs(endX,endY)) {
        cout << "Ponto de partida ou chegada invalido/inacessivel.\n";
        return {};
    }
    if (startX == endX && startY == endY) {
        metrics.pathLength = 1; metrics.pathCost = 0;
        return {{startX, startY}};
    }

    const int total = gridWidth * gridHeight;

    // Estruturas de dados particionadas (cada thread acessa apenas seus nós)
    vector<Node> nodes(total);
    initNodes(nodes, gridWidth, gridHeight);
    vector<char> closed(total, 0);  // Char evita problemas com bits

    // Estruturas por thread
    vector<unique_ptr<Inbox>> inboxes(numThreads);  // Caixa de mensagens para cada thread
    for (int t = 0; t < numThreads; t++) inboxes[t] = make_unique<Inbox>();

    vector<priority_queue<Node*, vector<Node*>, CompareNode>> opens(numThreads);  // Open list local

    // Variáveis de sincronização
    atomic<int>  bestCost{INT_MAX};  // Melhor custo encontrado
    atomic<bool> path_found{false};  // Sinaliza se caminho foi encontrado
    atomic<int>  idleThreadCount{0};      // Threads ociosas

    atomic<int>  messagesInFlight{0};  // Mensagens enviadas (não processadas ainda)

    vector<atomic<bool>> isThreadIdle(numThreads);  // Estado ocioso de cada thread
    for (int t = 0; t < numThreads; t++) isThreadIdle[t].store(false, memory_order_relaxed);

    vector<Metrics> threadMetrics(numThreads);  // Métricas por thread

    // Envia mensagem para thread "to" (propõe novo custo para um nó)
    auto sendMsg = [&](int to, const Message& msg) {
        messagesInFlight.fetch_add(1, memory_order_relaxed);

        omp_set_lock(&inboxes[to]->lock);
        inboxes[to]->msgs.push_back(msg);
        // Acorda thread se estava dormindo
        if (isThreadIdle[to].load(memory_order_relaxed)) {
            isThreadIdle[to].store(false, memory_order_relaxed);
            idleThreadCount.fetch_sub(1, memory_order_relaxed);
        }
        omp_unset_lock(&inboxes[to]->lock);
    };

    // Inicializa nó de partida na open list do seu dono
    {
        Node& startNode = nodes[flat(startX, startY, gridHeight)];
        startNode.g = 0; startNode.h = octile(startX, startY, endX, endY); startNode.f = startNode.h;
        opens[stateOwner(startX, startY, numThreads)].push(&startNode);
    }

    // Começa paralelização: P threads executam HDA* simultaneamente
    #pragma omp parallel num_threads(numThreads)
    {
        int threadId = omp_get_thread_num();
        auto&    threadOpenList  = opens[threadId];
        Inbox&   threadInbox = *inboxes[threadId];
        Metrics& threadMetricsLocal     = threadMetrics[threadId];
        bool     done    = false;

        while (!done) {
            // Verifica se caminho já foi encontrado por outra thread
            if (path_found.load(memory_order_relaxed)) break;

            // Processa mensagens recebidas de outras threads
            {
                vector<Message> recv;
                omp_set_lock(&threadInbox.lock);
                recv.swap(threadInbox.msgs);
                omp_unset_lock(&threadInbox.lock);

                for (const auto& msg : recv) {
                    messagesInFlight.fetch_sub(1, memory_order_relaxed);

                    int neighborNodeIndex = flat(msg.x, msg.y, gridHeight);
                    if (closed[neighborNodeIndex]) continue;  // Nó já expandido, ignora
                    Node& neighborNode = nodes[neighborNodeIndex];
                    if (msg.g < neighborNode.g) {        // Caminho mais barato encontrado
                        neighborNode.g        = msg.g;
                        neighborNode.h        = octile(msg.x, msg.y, endX, endY);
                        neighborNode.f        = neighborNode.g + neighborNode.h;
                        neighborNode.parent_x = msg.parent_x;
                        neighborNode.parent_y = msg.parent_y;
                        threadOpenList.push(&neighborNode);
                        threadMetricsLocal.neighborsAdded++;
                    }
                }
            }

            // Se open list vazia, tenta dormir (espera por mensagens)
            if (threadOpenList.empty()) {
                omp_set_lock(&threadInbox.lock);
                if (threadInbox.msgs.empty()) {
                    // Confirmado: inbox E open vazios → ocioso
                    isThreadIdle[threadId].store(true, memory_order_relaxed);
                    idleThreadCount.fetch_add(1, memory_order_relaxed);
                    omp_unset_lock(&threadInbox.lock);

                    // Spin-wait: aguarda ser acordado por sendMsg ou término global
                    while (isThreadIdle[threadId].load(memory_order_relaxed)) {
                        // Termina se todas threads ociosas E nenhuma mensagem pendente
                        if (idleThreadCount.load(memory_order_relaxed) == numThreads &&
                            messagesInFlight.load(memory_order_relaxed)  == 0) {
                            done = true;
                            break;
                        }
                        // Verifica novamente se caminho foi encontrado
                        if (path_found.load(memory_order_relaxed)) {
                            bool expected = true;
                            if (isThreadIdle[threadId].compare_exchange_strong(expected, false,
                                    memory_order_relaxed)) {
                                idleThreadCount.fetch_sub(1, memory_order_relaxed);
                            }
                            done = true;
                            break;
                        }
                        #pragma omp flush  // Força releitura de variáveis compartilhadas
                    }
                } else {
                    // Mensagem chegou enquanto verificava: tenta novamente
                    omp_unset_lock(&threadInbox.lock);
                }
                continue;
            }

            // Remove nós que não podem melhorar a solução (poda)
            {
                int bestFoundCost = bestCost.load(memory_order_relaxed);
                while (!threadOpenList.empty() && threadOpenList.top()->f >= bestFoundCost)
                    threadOpenList.pop();
                if (threadOpenList.empty()) continue;
            }

            // Expande nó de menor custo da open list
            Node* currentNode = threadOpenList.top(); threadOpenList.pop();
            int currentNodeIndex = flat(currentNode->x, currentNode->y, gridHeight);

            // Já foi expandido? Ignora (lazy deletion)
            if (closed[currentNodeIndex]) continue;
            closed[currentNodeIndex] = 1;  // Marca como expandido
            threadMetricsLocal.expandedNodes++;

            if ((long long)threadOpenList.size() > threadMetricsLocal.maxOpenListSize)
                threadMetricsLocal.maxOpenListSize = (long long)threadOpenList.size();

            // Chegou ao destino? Caminho encontrado!
            if (currentNode->x == endX && currentNode->y == endY) {
                int cost = cur->g;
                int prev = INT_MAX;
                while (!bestCost.compare_exchange_weak(prev, cost, memory_order_relaxed)) {
                    if (prev <= cost) break;
                }
                path_found.store(true, memory_order_seq_cst);
                done = true;
                break;
            }

            // Processa vizinhos (gera novos nós ou envia propostas)
            for (int i = 0; i < 8; i++) {
                int neighborX = currentNode->x + DX[i];
                int neighborY = currentNode->y + DY[i];
                threadMetricsLocal.neighborsChecked++;

                if (!isValid(neighborX, neighborY, gridWidth, gridHeight))   continue;
                if (grid[flat(neighborX, neighborY, gridHeight)] == 1) continue;  // Obstáculo
                if (closed[flat(neighborX, neighborY, gridHeight)])    continue;  // Já expandido

                int movementCost = (DX[i] != 0 && DY[i] != 0) ? 14 : 10;  // Diagonal/Cardinal
                int tentativeG    = currentNode->g + movementCost;
                int tentativeH    = octile(neighborX, neighborY, endX, endY);

                if (tentativeG + tentativeH >= bestCost.load(memory_order_relaxed)) continue;

                int nodeOwner = stateOwner(neighborX, neighborY, numThreads);

                if (nodeOwner == threadId) {
                    // Nó pertence a esta thread: atualiza diretamente
                    Node& neighborNode = nodes[flat(neighborX, neighborY, gridHeight)];
                    if (tentativeG < neighborNode.g) {
                        neighborNode.g        = tentativeG;
                        neighborNode.h        = tentativeH;
                        neighborNode.f        = tentativeG + tentativeH;
                        neighborNode.parent_x = currentNode->x;
                        neighborNode.parent_y = currentNode->y;
                        threadOpenList.push(&neighborNode);
                        threadMetricsLocal.neighborsAdded++;
                    }
                } else {
                    // Nó pertence a outra thread: envia mensagem
                    sendMsg(nodeOwner, {neighborX, neighborY, tentativeG, currentNode->x, currentNode->y});
                }
            }
        }
    } // Fim da paralelização

    // Fase sequencial: coleta resultados

    // Consolida estatísticas de todas as threads
    for (int t = 0; t < numThreads; t++) {
        metrics.expandedNodes    += threadMetrics[t].expandedNodes;
        metrics.neighborsChecked += threadMetrics[t].neighborsChecked;
        metrics.neighborsAdded   += threadMetrics[t].neighborsAdded;
        metrics.maxOpenListSize  += threadMetrics[t].maxOpenListSize;
    }

    if (!path_found.load()) return {};

    // Reconstrói caminho seguindo ponteiros parent
    vector<pair<int,int>> path;
    for (int cx = endX, cy = endY; cx != -1; ) {
        path.push_back({cx, cy});
        const Node& n = nodes[flat(cx, cy, gridHeight)];
        int px = n.parent_x, py = n.parent_y;
        cx = px; cy = py;
    }
    reverse(path.begin(), path.end());

    metrics.pathCost   = bestCost.load();
    metrics.pathLength = (int)path.size();
    return path;
}

// ===== VISUALIZAÇÃO =====

void visualizePath(const vector<int>& grid,
                   const vector<pair<int,int>>& path,
                   int sX, int sY, int eX, int eY,
                   int gridWidth, int gridHeight) {
    vector<vector<char>> vis(gridWidth, vector<char>(gridHeight, '.'));
    for (int i = 0; i < gX; ++i)
        for (int j = 0; j < gY; ++j)
            if (grid[flat(i, j, gY)]) vis[i][j] = '#';
    for (auto& p : path) {
        if      (p.first == sX && p.second == sY) vis[p.first][p.second] = 'S';
        else if (p.first == eX && p.second == eY) vis[p.first][p.second] = 'E';
        else                                        vis[p.first][p.second] = 'P';
    }
    cout << "Visualizacao (S=Inicio, E=Fim, P=Caminho, #=Obstaculo, .=Livre):\n";
    for (int i = 0; i < gX; ++i) {
        for (int j = 0; j < gY; ++j) cout << vis[i][j];
        cout << '\n';
    }
    cout << '\n';
}

// Saída
ofstream abrirCSV(const string& path) {
    ofstream f(path, ios::app);
    if (!f) { cerr << "Erro ao abrir CSV: " << path << "\n"; exit(1); }
    f.seekp(0, ios::end);
    if (f.tellp() == 0)
        f << "Teste,"
             "Tamanho do Grid,"
             "Percentual Obstaculos (%),"
             "Tempo de Execucao (s),"
             "Tempo de Execucao (ms),"
             "Nos Expandidos,"
             "Vizinhos Avaliados,"
             "Vizinhos Inseridos na Lista Aberta,"
             "Tamanho Maximo da Lista Aberta,"
             "Comprimento do Caminho (passos),"
             "Custo Total do Caminho\n";
    return f;
}

void salvaResultado(ofstream& f, int tn, const string& gs,
                    int op, double t, const Metrics& m) {
    long long ms = static_cast<long long>(t * 1000.0 + 0.5);
    f << tn << ","
      << gs << ","
      << op << ","
      << fixed << setprecision(3) << t << ","
      << ms << ","
      << m.expandedNodes    << ","
      << m.neighborsChecked << ","
      << m.neighborsAdded   << ","
      << m.maxOpenListSize  << ","
      << m.pathLength       << ","
      << m.pathCost         << "\n";
}

vector<GridInfo> getGrids() {
    vector<GridInfo> grids;
    for (int s : sizes)
        for (int d : densities) {
            GridInfo g;
            g.filename = basePath
                       + to_string(s) + "x" + to_string(s)
                       + "_" + to_string(d) + ".bin";
            g.name = to_string(s) + "x" + to_string(s) + "_" + to_string(d);
            g.obstaclePercentage = d;
            grids.push_back(g);
        }
    return grids;
}

int main() {
    auto grids = getGrids();
    auto csv   = abrirCSV("results/parallel_hda.csv");

    cout << "HDA* com " << NUM_THREADS << " threads (heuristica octile)\n\n";

    for (const auto& info : grids) {
        int gX = 0, gY = 0;
        vector<int> grid;

        cout << "Carregando grade " << info.name << "...\n";
        if (!loadGridFromFile(info.filename, grid, gX, gY)) {
            cerr << "Falha ao carregar " << info.filename << "\n";
            continue;
        }

        int sX = 0, sY = 0, eX = gX - 1, eY = gY - 1;
        grid[flat(sX, sY, gY)] = 0;
        grid[flat(eX, eY, gY)] = 0;

        for (int trial = 1; trial <= num_trials; ++trial) {
            Metrics m;
            auto t0   = chrono::high_resolution_clock::now();
            auto path = parallelDistributedAStarSearch(grid, sX, sY, eX, eY, gX, gY, m);
            auto t1   = chrono::high_resolution_clock::now();
            double elapsed = chrono::duration<double>(t1 - t0).count();

            if (!path.empty() && gX <= 20 && gY <= 20)
                visualizePath(grid, path, sX, sY, eX, eY, gX, gY);

            cout << "  trial " << trial
                 << " | tempo: " << fixed << setprecision(3) << elapsed << "s"
                 << " | nos: " << m.expandedNodes
                 << " | custo: " << m.pathCost << "\n";

            salvaResultado(csv, test_num++, info.name,
                           info.obstaclePercentage, elapsed, m);
        }
        cout << "\n";
    }

    csv.close();
    return 0;
}