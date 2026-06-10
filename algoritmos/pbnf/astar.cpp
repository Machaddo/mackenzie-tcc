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

// ========================= Config =========================
int test_num = 1;
const int num_trials  = 5;
const int NUM_THREADS = 4;
const int BLOCK_SIZE  = 100;

string basePath = "gridsTeste/";
vector<int> sizes     = {7000};
vector<int> densities = {30};

// ========================= Structs =========================
struct Node {
    int x, y;
    int f, g, h;
    int parent_x, parent_y;
};

struct CompareNode {
    bool operator()(const Node* a, const Node* b) const { return a->f > b->f; }
};

struct Metrics {
    long long expandedNodes    = 0;
    long long neighborsChecked = 0;
    long long neighborsAdded   = 0;
    long long maxOpenListSize  = 0;
    int pathLength = 0;
    int pathCost   = 0;
};

struct GridInfo {
    string filename, name;
    int obstaclePercentage;
};

// ========================= NBlock =========================
struct NBlock {
    int id;

    omp_lock_t lock;
    priority_queue<Node*, vector<Node*>, CompareNode> open;

    atomic<int>  f_min;
    atomic<bool> reserved;

    NBlock() : id(-1) {
        f_min.store(INT_MAX, memory_order_relaxed);
        reserved.store(false, memory_order_relaxed);
        omp_init_lock(&lock);
    }
    ~NBlock() { omp_destroy_lock(&lock); }

    NBlock(const NBlock&)            = delete;
    NBlock& operator=(const NBlock&) = delete;
};

struct BEntry {
    int f_min;
    int id;
    bool operator>(const BEntry& o) const { return f_min > o.f_min; }
};

// ========================= Helpers =========================
inline int flat(int x, int y, int W) { return x * W + y; }

bool loadGridFromFile(const string& fn,
                      vector<int>& grid,
                      int& gX, int& gY) {
    ifstream file(fn, ios::binary);
    if (!file) { cerr << "Erro ao abrir " << fn << "\n"; return false; }
    file.read(reinterpret_cast<char*>(&gX), sizeof(int));
    file.read(reinterpret_cast<char*>(&gY), sizeof(int));
    grid.resize(gX * gY);
    for (int i = 0; i < gX; ++i)
        for (int j = 0; j < gY; ++j) {
            char c; file.read(&c, sizeof(char));
            grid[flat(i, j, gY)] = c;
        }
    return true;
}

bool isValid(int x, int y, int gX, int gY) {
    return x >= 0 && x < gX && y >= 0 && y < gY;
}

inline int octile(int x1, int y1, int x2, int y2) {
    int dx = abs(x1 - x2), dy = abs(y1 - y2);
    return 10 * (dx + dy) - 6 * min(dx, dy);
}

void initNodes(vector<Node>& nodes, int gX, int gY) {
    nodes.resize(gX * gY);
    for (int i = 0; i < gX; ++i)
        for (int j = 0; j < gY; ++j) {
            auto& n = nodes[flat(i, j, gY)];
            n.x = i; n.y = j;
            n.f = n.g = n.h = INT_MAX / 2;
            n.parent_x = n.parent_y = -1;
        }
}

const int DX[] = {-1, -1, -1,  0,  0,  1,  1,  1};
const int DY[] = {-1,  0,  1, -1,  1, -1,  0,  1};

// ========================= PBNF =========================
vector<pair<int,int>> pbnfSearch(
    const vector<int>& grid,
    int startX, int startY,
    int endX, int endY,
    int gridWidth, int gridHeight,
    Metrics& metrics,
    int numThreads = NUM_THREADS)
{
    auto isObstacle = [&](int x, int y) { return grid[flat(x, y, gridHeight)] == 1; };

    if (!isValid(startX,startY,gridWidth,gridHeight) || !isValid(endX,endY,gridWidth,gridHeight) ||
        isObstacle(startX,startY) || isObstacle(endX,endY)) {
        cout << "Ponto de partida ou chegada invalido/inacessivel.\n";
        return {};
    }
    if (startX == endX && startY == endY) {
        metrics.pathLength = 1; metrics.pathCost = 0;
        return {{startX, startY}};
    }

    const int blocksCountX = (gridWidth + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int blocksCountY = (gridHeight + BLOCK_SIZE - 1) / BLOCK_SIZE;
    const int totalBlocks  = blocksCountX * blocksCountY;

    auto getBlockId = [&](int x, int y) -> int {
        return (x / BLOCK_SIZE) * blocksCountY + (y / BLOCK_SIZE);
    };

    vector<unique_ptr<NBlock>> blocks(totalBlocks);
    for (int i = 0; i < totalBlocks; i++) {
        blocks[i]     = make_unique<NBlock>();
        blocks[i]->id = i;
    }

    priority_queue<BEntry, vector<BEntry>, greater<BEntry>> blockQueue;
    omp_lock_t blockQueueLock;
    omp_init_lock(&blockQueueLock);

    const int totalNodes = gridWidth * gridHeight;
    vector<Node> nodeArray(totalNodes);
    initNodes(nodeArray, gridWidth, gridHeight);
    vector<char> closedSet(totalNodes, 0);

    atomic<int>  bestPathCost{INT_MAX};
    atomic<bool> pathFound{false};
    atomic<int>  activeThreadCount{0};
    atomic<int>  globalBestF{INT_MAX};

    vector<Metrics> threadMetrics(numThreads);

    // Semeadura do nó inicial
    {
        int startNodeIndex = flat(startX, startY, gridHeight);
        nodeArray[startNodeIndex].g = 0;
        nodeArray[startNodeIndex].h = octile(startX, startY, endX, endY);
        nodeArray[startNodeIndex].f = nodeArray[startNodeIndex].h;

        int startBlockId      = getBlockId(startX, startY);
        NBlock& startBlock = *blocks[startBlockId];
        startBlock.open.push(&nodeArray[startNodeIndex]);
        startBlock.f_min.store(nodeArray[startNodeIndex].f, memory_order_relaxed);

        blockQueue.push({nodeArray[startNodeIndex].f, startBlockId});
        globalBestF.store(nodeArray[startNodeIndex].f, memory_order_relaxed);
    }

    #pragma omp parallel num_threads(numThreads)
    {
        int threadId      = omp_get_thread_num();
        Metrics& currentThreadMetrics = threadMetrics[threadId];

        while (true) {

            // Verifica término por caminho encontrado
            if (pathFound.load(memory_order_relaxed)) break;

            // Tenta obter o bloco mais promissor
            NBlock* assignedBlock = nullptr;
            {
                omp_set_lock(&blockQueueLock);

                while (!blockQueue.empty()) {
                    BEntry top = blockQueue.top();
                    blockQueue.pop();

                    NBlock* candidateBlock    = blocks[top.id].get();
                    int blockMinF = candidateBlock->f_min.load(memory_order_relaxed);

                    if (top.f_min > blockMinF) continue;
                    if (blockMinF == INT_MAX)   continue;
                    if (candidateBlock->reserved.load(memory_order_relaxed)) continue;

                    bool wasNotReserved = false;
                    if (candidateBlock->reserved.compare_exchange_strong(
                            wasNotReserved, true, memory_order_relaxed)) {
                        assignedBlock = candidateBlock;
                        break;
                    }
                }

                globalBestF.store(
                    blockQueue.empty() ? INT_MAX : blockQueue.top().f_min,
                    memory_order_relaxed);

                omp_unset_lock(&blockQueueLock);
            }

            // Sem bloco disponível → verifica término
            if (!assignedBlock) {
                // Checa pathFound explicitamente antes do spin
                if (pathFound.load(memory_order_acquire)) break;

                if (activeThreadCount.load(memory_order_relaxed) == 0) {
                    omp_set_lock(&blockQueueLock);
                    bool empty = blockQueue.empty();
                    omp_unset_lock(&blockQueueLock);
                    if (empty) break;
                }
                // Substituído #pragma omp flush por taskyield —
                // evita flush total de todas as variáveis compartilhadas,
                // reduzindo drasticamente o custo do spin-wait em grids grandes.
                #pragma omp taskyield
                continue;
            }

            // Reserva obtida — começa a expansão
            activeThreadCount.fetch_add(1, memory_order_relaxed);

            // Loop de expansão dentro do bloco
            while (!pathFound.load(memory_order_relaxed)) {

                // Condição de rendição
                {
                    int blockBestF   = assignedBlock->f_min.load(memory_order_relaxed);
                    int globalBestFValue = globalBestF.load(memory_order_relaxed);

                    if (blockBestF > globalBestFValue && globalBestFValue != INT_MAX) break;

                    if (blockBestF >= bestPathCost.load(memory_order_relaxed)) {
                        omp_set_lock(&assignedBlock->lock);
                        while (!assignedBlock->open.empty()) assignedBlock->open.pop();
                        assignedBlock->f_min.store(INT_MAX, memory_order_relaxed);
                        omp_unset_lock(&assignedBlock->lock);
                        break;
                    }
                }

                // Extrai melhor nó do bloco
                Node* currentNode = nullptr;
                {
                    omp_set_lock(&assignedBlock->lock);

                    while (!assignedBlock->open.empty()) {
                        Node* top = assignedBlock->open.top();
                        if (closedSet[flat(top->x, top->y, gridHeight)]) {
                            assignedBlock->open.pop();
                        } else {
                            break;
                        }
                    }

                    if (assignedBlock->open.empty()) {
                        assignedBlock->f_min.store(INT_MAX, memory_order_relaxed);
                        omp_unset_lock(&assignedBlock->lock);
                        break;
                    }

                    currentNode = assignedBlock->open.top();
                    assignedBlock->open.pop();
                    int currentNodeIndex = flat(currentNode->x, currentNode->y, gridHeight);

                    if (closedSet[currentNodeIndex]) {
                        assignedBlock->f_min.store(
                            assignedBlock->open.empty() ? INT_MAX : assignedBlock->open.top()->f,
                            memory_order_relaxed);
                        omp_unset_lock(&assignedBlock->lock);
                        continue;
                    }

                    closedSet[currentNodeIndex] = 1;
                    assignedBlock->f_min.store(
                        assignedBlock->open.empty() ? INT_MAX : assignedBlock->open.top()->f,
                        memory_order_relaxed);

                    omp_unset_lock(&assignedBlock->lock);
                }

                currentThreadMetrics.expandedNodes++;

                if ((long long)assignedBlock->open.size() > currentThreadMetrics.maxOpenListSize)
                    currentThreadMetrics.maxOpenListSize = (long long)assignedBlock->open.size();

                // Nó destino atingido, verficar
                if (currentNode->x == endX && currentNode->y == endY) {
                    int cost = currentNode->g;
                    int prev = INT_MAX;
                    while (!bestPathCost.compare_exchange_weak(
                               prev, cost, memory_order_relaxed)) {
                        if (prev <= cost) break;
                    }
                    pathFound.store(true, memory_order_seq_cst);
                    break;
                }

                // Expande vizinhos
                for (int i = 0; i < 8; i++) {
                    int neighborX = currentNode->x + DX[i];
                    int neighborY = currentNode->y + DY[i];
                    currentThreadMetrics.neighborsChecked++;

                    if (!isValid(neighborX, neighborY, gridWidth, gridHeight))   continue;
                    if (grid[flat(neighborX, neighborY, gridHeight)] == 1) continue;

                    int neighborIndex = flat(neighborX, neighborY, gridHeight);

                    if (closedSet[neighborIndex]) continue;

                    int movementCost = (DX[i] != 0 && DY[i] != 0) ? 14 : 10;
                    int tentativeG    = currentNode->g + movementCost;
                    int tentativeH    = octile(neighborX, neighborY, endX, endY);

                    if (tentativeG + tentativeH >= bestPathCost.load(memory_order_relaxed)) continue;

                    int    neighborBlockId   = getBlockId(neighborX, neighborY);
                    NBlock& targetBlock   = *blocks[neighborBlockId];
                    bool improved = false;
                    int  neighborF    = tentativeG + tentativeH;

                    // [FIX 1] Mesmo bloco: o ramo sem lock foi removido.
                    // A reserva impede OUTROS threads de reservar o bloco, mas
                    // NÃO impede cross-updates de threads que já detectaram
                    // este bloco como alvo. Portanto o lock é obrigatório em
                    // AMBOS os casos para proteger a priority_queue.
                    omp_set_lock(&targetBlock.lock);

                    if (!closedSet[neighborIndex] && tentativeG < nodeArray[neighborIndex].g) {
                        nodeArray[neighborIndex].g        = tentativeG;
                        nodeArray[neighborIndex].h        = tentativeH;
                        nodeArray[neighborIndex].f        = neighborF;
                        nodeArray[neighborIndex].parent_x = currentNode->x;
                        nodeArray[neighborIndex].parent_y = currentNode->y;
                        targetBlock.open.push(&nodeArray[neighborIndex]);
                        currentThreadMetrics.neighborsAdded++;

                        int blockMinFValue = targetBlock.f_min.load(memory_order_relaxed);
                        if (neighborF < blockMinFValue)
                            targetBlock.f_min.store(neighborF, memory_order_relaxed);

                        // Só notifica o blockQueue para blocos diferentes do atual.
                        // O bloco atual já está reservado e será re-inserido no
                        // step 6 após a liberação — inserir agora seria redundante
                        // e geraria entradas duplicadas desnecessárias.
                        if (neighborBlockId != assignedBlock->id)
                            improved = true;
                    }

                    omp_unset_lock(&targetBlock.lock);

                    if (improved) {
                        omp_set_lock(&blockQueueLock);
                        blockQueue.push({neighborF, neighborBlockId});
                        globalBestF.store(
                            blockQueue.top().f_min, memory_order_relaxed);
                        omp_unset_lock(&blockQueueLock);
                    }
                }

            } // fim do loop de expansão

            // Libera a reserva
            assignedBlock->reserved.store(false, memory_order_relaxed);

            int remainingBlockF = assignedBlock->f_min.load(memory_order_relaxed);
            if (remainingBlockF < INT_MAX &&
                remainingBlockF < bestPathCost.load(memory_order_relaxed) &&
                !pathFound.load(memory_order_relaxed)) {
                omp_set_lock(&blockQueueLock);
                blockQueue.push({remainingBlockF, assignedBlock->id});
                globalBestF.store(blockQueue.top().f_min, memory_order_relaxed);
                omp_unset_lock(&blockQueueLock);
            }

            // Decrementa somente após garantir que o trabalho restante já está visível na fila global.
            activeThreadCount.fetch_sub(1, memory_order_relaxed);

        } // fim do loop externo

    } // fim da região paralela

    omp_destroy_lock(&blockQueueLock);

    for (int t = 0; t < numThreads; t++) {
        metrics.expandedNodes    += threadMetrics[t].expandedNodes;
        metrics.neighborsChecked += threadMetrics[t].neighborsChecked;
        metrics.neighborsAdded   += threadMetrics[t].neighborsAdded;
        metrics.maxOpenListSize  += threadMetrics[t].maxOpenListSize;
    }

    if (!pathFound.load()) return {};

    vector<pair<int,int>> path;
    for (int currentX = endX, currentY = endY; currentX != -1; ) {
        path.push_back({currentX, currentY});
        const Node& n = nodeArray[flat(currentX, currentY, gridHeight)];
        int parentX = n.parent_x, parentY = n.parent_y;
        currentX = parentX; currentY = parentY;
    }
    reverse(path.begin(), path.end());

    metrics.pathCost   = bestPathCost.load();
    metrics.pathLength = (int)path.size();
    return path;
}

// ========================= Visualização =========================
void visualizePath(const vector<int>& grid,
                   const vector<pair<int,int>>& path,
                   int sX, int sY, int eX, int eY,
                   int gX, int gY) {
    vector<vector<char>> vis(gX, vector<char>(gY, '.'));
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

// ========================= CSV =========================
ofstream abrirCSV(const string& path) {
    // Verifica se o arquivo ja existe e tem conteudo ANTES de abrir em append.
    // Chamar seekp/tellp num ofstream em ios::app pode ativar o failbit em
    // diversas implementacoes do C++, tornando todos os writes subsequentes
    // silenciosamente ignorados — esse era o bug que impedia o CSV de ser salvo.
    bool needHeader = true;
    {
        ifstream check(path, ios::binary | ios::ate);
        if (check && check.tellg() > 0)
            needHeader = false;
    }

    // Garante que o diretorio de saida existe
    {
        string dir = path.substr(0, path.find_last_of("/\\"));
        if (!dir.empty())
            system(("mkdir -p \"" + dir + "\"").c_str());
    }

    ofstream f(path, ios::app);
    if (!f) { cerr << "Erro ao abrir CSV: " << path << "\n"; exit(1); }

    if (needHeader)
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
    f.flush();
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

// ========================= Main =========================
int main() {
    auto grids = getGrids();
    auto csv   = abrirCSV("results/parallel_pbnf.csv");

    cout << "PBNF com " << NUM_THREADS << " threads"
         << ", bloco " << BLOCK_SIZE << "x" << BLOCK_SIZE
         << ", heuristica octile\n\n";

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
            auto path = pbnfSearch(grid, sX, sY, eX, eY, gX, gY, m);
            auto t1   = chrono::high_resolution_clock::now();
            double elapsed = chrono::duration<double>(t1 - t0).count();

            if (!path.empty() && gX <= 20 && gY <= 20)
                visualizePath(grid, path, sX, sY, eX, eY, gX, gY);

            cout << "  trial " << trial
                 << " | tempo: " << fixed << setprecision(3) << elapsed << "s"
                 << " | nos: "   << m.expandedNodes
                 << " | custo: " << m.pathCost << "\n";

            salvaResultado(csv, test_num++, info.name,
                           info.obstaclePercentage, elapsed, m);
            csv.flush();
        }
        cout << "\n";
    }

    csv.close();
    return 0;
}