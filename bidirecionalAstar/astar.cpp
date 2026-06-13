// Compilar com: g++ astar.cpp -o astar.exe -fopenmp -O2 -std=c++17
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
#include <omp.h>

using namespace std;

// Config
int test_num = 1;
const int num_trials = 20;

std::string basePath = "gridsTeste/";
std::vector<int> sizes     = {5000, 6000, 7000};
std::vector<int> densities = {0, 10, 20, 30};

//Structs 
struct Node {
    int x, y;
    int f, g, h;
    int parent_x, parent_y;
};

struct CompareNode {
    bool operator()(const Node* a, const Node* b) const { return a->f > b->f; }
};

// Métricas
struct Metrics {
    long long expandedNodes    = 0;
    long long neighborsChecked = 0;
    long long neighborsAdded   = 0;
    long long maxOpenListSize  = 0;
    int pathLength = 0;
    int pathCost   = 0;
};

struct GridInfo {
    std::string filename, name;
    int obstaclePercentage;
};

// Helpers 
inline int flat(int x, int y, int W) { return x * W + y; }

bool loadGridFromFile(const std::string& fn,
                      std::vector<int>& grid,
                      int& gX, int& gY) {
    std::ifstream file(fn, std::ios::binary);
    if (!file) { std::cerr << "Erro ao abrir " << fn << "\n"; return false; }
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

// Heurística octile — admissível para movimento 8-direcional com custos 10/14.
// Fórmula: 10*(dx+dy) - 6*min(dx,dy)
// O custo real de (dx,dy) movimentos é: 14*min(dx,dy) + 10*(max-min)
// A heurística iguala esse custo quando não há obstáculos → admissível e consistente.
inline int octile(int x1, int y1, int x2, int y2) {
    int dx = abs(x1 - x2), dy = abs(y1 - y2);
    return 10 * (dx + dy) - 6 * std::min(dx, dy);
}

void initNodes(std::vector<Node>& nodes, int gX, int gY) {
    nodes.resize(gX * gY);
    for (int i = 0; i < gX; ++i)
        for (int j = 0; j < gY; ++j) {
            auto& n = nodes[flat(i, j, gY)];
            n.x = i; n.y = j;
            n.f = n.g = n.h = INT_MAX / 2; //2 para evitar overflow em adições
            n.parent_x = n.parent_y = -1;
        }
}

const int DX[] = {-1, -1, -1,  0,  0,  1,  1,  1};
const int DY[] = {-1,  0,  1, -1,  1, -1,  0,  1};

// Bidirectional A* 
//
// ABORDAGEM DE PARALELIZAÇÃO:
//   Thread 1: Busca FORWARD  — expande do start em direção ao end
//   Thread 2: Busca BACKWARD — expande do end   em direção ao start
//
// Quando um nó é fechado por uma busca e está na closed list da outra,
// temos um caminho candidato com custo g_fwd(u) + g_bwd(u).
// O algoritmo encerra quando ambas as open lists têm min_f >= bestCost.
//
// Vantagem real: as duas buscas exploram espaços independentes e reduzem
// em ~50% os nós expandidos em relação ao A* unidirecional.
//
// SINCRONIZAÇÃO:
//   - Closed lists: std::atomic<bool> por slot.
//     Escrita com memory_order_release, leitura com memory_order_acquire.
//     Garante que o valor de 'g' escrito antes do release seja visível
//     para a thread que faz o acquire —sem lock.
//   - bestCost: atualizado via compare_exchange_weak (CAS lock-free).
//   - meetX / meetY: protegidos por omp_lock_t apenas na escrita.
//   - searchDone / topFFwd / topFBwd: std::atomic<int> com relaxed ordering.
//     Usados apenas para a condição de término

std::vector<std::pair<int,int>> biAStarSearch(
    const std::vector<int>& grid,
    int startX, int startY,
    int endX, int endY,
    int gridWidth, int gridHeight,
    Metrics& metrics)
{
    auto isObs = [&](int x, int y) { return grid[flat(x, y, gridHeight)] == 1; };

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

    // arrays flat para nós de cada direção, melhor localidade de cache
    std::vector<Node> forwardNodes(total), backwardNodes(total);
    initNodes(forwardNodes, gridWidth, gridHeight);
    initNodes(backwardNodes, gridWidth, gridHeight);

    // Closed lists com atomic<bool>:
    // Release na escrita garante que o valor de 'g' seja visível pela outra thread
    // quando ela fizer o acquire-load do mesmo slot.
    std::vector<std::atomic<bool>> forwardClosed(total), backwardClosed(total);
    for (auto& b : forwardClosed) b.store(false, std::memory_order_relaxed);
    for (auto& b : backwardClosed) b.store(false, std::memory_order_relaxed);

    // Inicializa nós de partida de cada direção
    {
        auto& s = forwardNodes[flat(startX, startY, gridHeight)];
        s.g = 0; s.h = octile(startX, startY, endX, endY); s.f = s.h;
    }
    {
        auto& e = backwardNodes[flat(endX, endY, gridHeight)];
        e.g = 0; e.h = octile(endX, endY, startX, startY); e.f = e.h;
    }

    priority_queue<Node*, vector<Node*>, CompareNode> forwardOpen, backwardOpen;
    forwardOpen.push(&forwardNodes[flat(startX, startY, gridHeight)]);
    backwardOpen.push(&backwardNodes[flat(endX, endY, gridHeight)]);

    // Estado compartilhado entre as duas threads
    std::atomic<int>  bestCost{INT_MAX};
    std::atomic<bool> searchDone{false};
    std::atomic<int>  topFFwd{octile(startX, startY, endX, endY)};
    std::atomic<int>  topFBwd{octile(endX, endY, startX, startY)};

    // meetX / meetY: protegidos por omp_lock_t (escritas raras)
    int meetX = -1, meetY = -1;
    omp_lock_t meetLock;
    omp_init_lock(&meetLock);

    // Métricas separadas por direção — agregadas ao final, sem contenção
    Metrics forwardMetrics, backwardMetrics;

    // Lambda de busca — executada por cada thread
    auto doSearch = [&](
        int goalX, int goalY,
        std::vector<Node>& myNodes,
        std::vector<Node>& otherNodes,
        priority_queue<Node*, vector<Node*>, CompareNode>& myOpenList,
        std::vector<std::atomic<bool>>& myClosed,
        std::vector<std::atomic<bool>>& otherClosed,
        std::atomic<int>& myTopF,
        std::atomic<int>& otTopF,
        Metrics& m
    ) {
        while (!myOpenList.empty() && !searchDone.load(std::memory_order_relaxed)) {

            // Publica menor f desta direção para a outra thread usar na condição de término
            myTopF.store(myOpenList.top()->f, std::memory_order_relaxed);

            // Condição de término ótima do Bidirectional A*:
            // Se ambas as open lists têm min_f >= bestCost, nenhuma expansão futura
            // pode encontrar um caminho melhor — o bestCost atual é ótimo.
            int currentBestCost = bestCost.load(std::memory_order_relaxed);
            if (currentBestCost < INT_MAX &&
                myTopF.load(std::memory_order_relaxed) >= currentBestCost &&
                otTopF.load(std::memory_order_relaxed) >= currentBestCost) {
                searchDone.store(true, std::memory_order_relaxed);
                break;
            }

            Node* cur = myOpenList.top();
            myOpenList.pop();

            int currentNodeIndex = flat(cur->x, cur->y, gridHeight);
            if (myClosed[currentNodeIndex].load(std::memory_order_relaxed)) continue;

            // garante visibilidade do g deste nó para a outra thread
            // após ela fazer acquire-load de myClosed[currentNodeIndex].
            myClosed[currentNodeIndex].store(true, std::memory_order_release);
            m.expandedNodes++;

            if ((long long)myOpenList.size() > m.maxOpenListSize)
                m.maxOpenListSize = (long long)myOpenList.size();

            for (int i = 0; i < 8; i++) {
                int neighborX = cur->x + DX[i];
                int neighborY = cur->y + DY[i];
                m.neighborsChecked++;

                if (!isValid(neighborX, neighborY, gridWidth, gridHeight))                             continue;
                if (grid[flat(neighborX, neighborY, gridHeight)] == 1)                          continue;
                if (myClosed[flat(neighborX, neighborY, gridHeight)].load(std::memory_order_relaxed)) continue;

                int moveCost    = (DX[i] != 0 && DY[i] != 0) ? 14 : 10;
                int tentative_g = cur->g + moveCost;

                Node* neighborNode = &myNodes[flat(neighborX, neighborY, gridHeight)];
                if (tentative_g < neighborNode->g) {
                    neighborNode->parent_x = cur->x;
                    neighborNode->parent_y = cur->y;
                    neighborNode->g        = tentative_g;
                    neighborNode->h        = octile(neighborX, neighborY, goalX, goalY);
                    neighborNode->f        = neighborNode->g + neighborNode->h;
                    myOpenList.push(neighborNode);
                    m.neighborsAdded++;

                    // Acquire: garante visibilidade do g escrito pela outra thread
                    // antes do release-store de otherClosed[neighborNodeIndex] = true.
                    int neighborNodeIndex = flat(neighborX, neighborY, gridHeight);
                    if (otherClosed[neighborNodeIndex].load(std::memory_order_acquire)) {
                        int otherG    = otherNodes[neighborNodeIndex].g;
                        int candidate = tentative_g + otherG;

                        // Atualiza bestCost via CAS lock-free
                        int curBest = bestCost.load(std::memory_order_relaxed);
                        while (candidate < curBest &&
                               !bestCost.compare_exchange_weak(
                                   curBest, candidate, std::memory_order_relaxed)) {}

                        // Registra ponto de encontro sob lock (escrita rara)
                        if (candidate <= bestCost.load(std::memory_order_relaxed)) {
                            omp_set_lock(&meetLock);
                            if (candidate <= bestCost.load(std::memory_order_relaxed)) {
                                meetX = neighborX;
                                meetY = neighborY;
                            }
                            omp_unset_lock(&meetLock);
                        }
                    }
                }
            }
        }
        myTopF.store(INT_MAX, std::memory_order_relaxed);
    };

    // Executa as duas buscas em paralelo com 2 threads OpenMP
    #pragma omp parallel sections num_threads(2)
    {
        #pragma omp section
        { doSearch(endX, endY, forwardNodes, backwardNodes, forwardOpen, forwardClosed, backwardClosed, topFFwd, topFBwd, forwardMetrics); }

        #pragma omp section
        { doSearch(startX, startY, backwardNodes, forwardNodes, backwardOpen, backwardClosed, forwardClosed, topFBwd, topFFwd, backwardMetrics); }
    }

    omp_destroy_lock(&meetLock);

    // Agrega métricas das duas direções
    metrics.expandedNodes    = forwardMetrics.expandedNodes    + backwardMetrics.expandedNodes;
    metrics.neighborsChecked = forwardMetrics.neighborsChecked + backwardMetrics.neighborsChecked;
    metrics.neighborsAdded   = forwardMetrics.neighborsAdded   + backwardMetrics.neighborsAdded;
    metrics.maxOpenListSize  = forwardMetrics.maxOpenListSize  + backwardMetrics.maxOpenListSize;

    int meetingX = meetX, meetingY = meetY;
    if (meetingX == -1) return {};

    // Reconstrução do caminho 
    // Parte forward: meeting start via parents de forwardNodes, depois invertida
    std::vector<std::pair<int,int>> pathFwd;
    for (int currentX = meetingX, currentY = meetingY; currentX != -1; ) {
        pathFwd.push_back({currentX, currentY});
        const Node& n = forwardNodes[flat(currentX, currentY, gridHeight)];
        int px = n.parent_x, py = n.parent_y;
        currentX = px; currentY = py;
    }
    std::reverse(pathFwd.begin(), pathFwd.end()); // start → meeting

    // Parte backward: parent do meeting em backwardNodes → end
    std::vector<std::pair<int,int>> pathBwd;
    {
        const Node& meetingNodeBackward = backwardNodes[flat(meetingX, meetingY, gridHeight)];
        for (int currentX = meetingNodeBackward.parent_x, currentY = meetingNodeBackward.parent_y; currentX != -1; ) {
            pathBwd.push_back({currentX, currentY});
            const Node& n = backwardNodes[flat(currentX, currentY, gridHeight)];
            int px = n.parent_x, py = n.parent_y;
            currentX = px; currentY = py;
        }
    }

    std::vector<std::pair<int,int>> fullPath;
    fullPath.reserve(pathFwd.size() + pathBwd.size());
    fullPath.insert(fullPath.end(), pathFwd.begin(), pathFwd.end());
    fullPath.insert(fullPath.end(), pathBwd.begin(), pathBwd.end());

    metrics.pathCost   = bestCost.load();
    metrics.pathLength = (int)fullPath.size();
    return fullPath;
}

//  Visualização 
void visualizePath(const std::vector<int>& grid,
                   const std::vector<std::pair<int,int>>& path,
                   int sX, int sY, int eX, int eY,
                   int gX, int gY) {
    std::vector<std::vector<char>> vis(gX, std::vector<char>(gY, '.'));
    for (int i = 0; i < gX; ++i)
        for (int j = 0; j < gY; ++j)
            if (grid[flat(i, j, gY)]) vis[i][j] = '#';
    for (auto& p : path) {
        if      (p.first == sX && p.second == sY) vis[p.first][p.second] = 'S';
        else if (p.first == eX && p.second == eY) vis[p.first][p.second] = 'E';
        else                                        vis[p.first][p.second] = 'P';
    }
    std::cout << "Visualizacao (S=Inicio, E=Fim, P=Caminho, #=Obstaculo, .=Livre):\n";
    for (int i = 0; i < gX; ++i) {
        for (int j = 0; j < gY; ++j) std::cout << vis[i][j];
        std::cout << '\n';
    }
    std::cout << '\n';
}

// CSV 
std::ofstream abrirCSV(const std::string& path) {
    std::ofstream f(path, std::ios::app);
    if (!f) { std::cerr << "Erro ao abrir CSV: " << path << "\n"; exit(1); }
    f.seekp(0, std::ios::end);
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

void salvaResultado(std::ofstream& f, int tn, const std::string& gs,
                    int op, double t, const Metrics& m) {
    long long ms = static_cast<long long>(t * 1000.0 + 0.5);
    f << tn << ","
      << gs << ","
      << op << ","
      << std::fixed << std::setprecision(3) << t << ","
      << ms << ","
      << m.expandedNodes    << ","
      << m.neighborsChecked << ","
      << m.neighborsAdded   << ","
      << m.maxOpenListSize  << ","
      << m.pathLength       << ","
      << m.pathCost         << "\n";
}

std::vector<GridInfo> getGrids() {
    std::vector<GridInfo> grids;
    for (int s : sizes)
        for (int d : densities) {
            GridInfo g;
            g.filename = basePath
                       + std::to_string(s) + "x" + std::to_string(s)
                       + "_" + std::to_string(d) + ".bin";
            g.name = std::to_string(s) + "x" + std::to_string(s) + "_" + std::to_string(d);
            g.obstaclePercentage = d;
            grids.push_back(g);
        }
    return grids;
}

int main() {
    auto grids = getGrids();
    auto csv   = abrirCSV("results/parallel_bidirectional.csv") ;

    for (const auto& info : grids) {
        int gX = 0, gY = 0;
        std::vector<int> grid;

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
            auto path = biAStarSearch(grid, sX, sY, eX, eY, gX, gY, m);
            auto t1   = chrono::high_resolution_clock::now();
            double elapsed = chrono::duration<double>(t1 - t0).count();

            if (!path.empty() && gX <= 20 && gY <= 20)
                visualizePath(grid, path, sX, sY, eX, eY, gX, gY);

            salvaResultado(csv, test_num++, info.name,
                           info.obstaclePercentage, elapsed, m);
        }
    }

    csv.close();
    return 0;
}