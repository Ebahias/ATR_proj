#include "comum/mapa.hpp"

#include <cmath>
#include <limits>

enum {
    B1, B2, B3, B4,
    W1, W2, W3, W4,
    E1, E2, E3, E4,
    ZA, ZB, ZC, ZD,
    N, S,
    P0A, P0B, P1A, P1B,
    R1, R2,
    NUM_NOS
};

static const NoMapa NOS[NUM_NOS] = {
    {"B1",  30,  15}, {"B2",  30,  45}, {"B3",  30,  75}, {"B4",  30, 105},
    {"W1",  50,  15}, {"W2",  50,  45}, {"W3",  50,  75}, {"W4",  50, 105},
    {"E1", 150,  15}, {"E2", 150,  45}, {"E3", 150,  75}, {"E4", 150, 105},
    {"ZA", 170,  15}, {"ZB", 170,  45}, {"ZC", 170,  75}, {"ZD", 170, 105},
    {"N",  100,  15}, {"S",  100, 105},
    {"P0a", 70,  45}, {"P0b", 130,  45}, {"P1a", 70,  75}, {"P1b", 130,  75},
    {"R1", 100,  30}, {"R2", 100,  90},
};

static const PassagemMapa PASSAGENS[] = {
    {"Corredor central norte", 1, P0A, P0B},
    {"Corredor central sul",   2, P1A, P1B},
    {"Corredor das baias",     3, W2,  W3},
};

#define NUM_PASSAGENS ((int)(sizeof(PASSAGENS) / sizeof(PASSAGENS[0])))

static_assert(NUM_PASSAGENS <= MAX_PASSAGENS, "aumente MAX_PASSAGENS");

static const ArestaMapa ARESTAS[] = {
    // Baias e zonas de entrega
    {B1, W1, -1}, {B2, W2, -1}, {B3, W3, -1}, {B4, W4, -1},
    {E1, ZA, -1}, {E2, ZB, -1}, {E3, ZC, -1}, {E4, ZD, -1},
    // Corredor oeste (o trecho W2-W3 é estreito)
    {W1, W2, -1}, {W2, W3, 2}, {W3, W4, -1},
    // Corredor leste
    {E1, E2, -1}, {E2, E3, -1}, {E3, E4, -1},
    // Corredores norte e sul
    {W1, N, -1}, {N, E1, -1}, {W4, S, -1}, {S, E4, -1},
    // Corredores centrais (estreitos no meio)
    {W2, P0A, -1}, {P0A, P0B, 0}, {P0B, E2, -1},
    {W3, P1A, -1}, {P1A, P1B, 1}, {P1B, E3, -1},
    // Recarga
    {N, R1, -1}, {S, R2, -1},
};

#define NUM_ARESTAS ((int)(sizeof(ARESTAS) / sizeof(ARESTAS[0])))

static const AreaMapa AREAS[] = {
    {AREA_BAIA, 1, "Baia 1",   0,   0,  40,  30, B1,  15,  15},
    {AREA_BAIA, 2, "Baia 2",   0,  30,  40,  60, B2,  15,  45},
    {AREA_BAIA, 3, "Baia 3",   0,  60,  40,  90, B3,  15,  75},
    {AREA_BAIA, 4, "Baia 4",   0,  90,  40, 120, B4,  15, 105},
    {AREA_ZONA, 1, "Zona A", 160,   0, 200,  30, ZA, 185,  15},
    {AREA_ZONA, 2, "Zona B", 160,  30, 200,  60, ZB, 185,  45},
    {AREA_ZONA, 3, "Zona C", 160,  60, 200,  90, ZC, 185,  75},
    {AREA_ZONA, 4, "Zona D", 160,  90, 200, 120, ZD, 185, 105},
    {AREA_RECARGA, 1, "Recarga 1", 90, 24, 110, 36, R1, 100,  30},
    {AREA_RECARGA, 2, "Recarga 2", 90, 84, 110, 96, R2, 100,  90},
};

#define NUM_AREAS ((int)(sizeof(AREAS) / sizeof(AREAS[0])))


int mapa_num_nos(void)                  { return NUM_NOS; }
const NoMapa& mapa_no(int i)            { return NOS[i]; }
int mapa_num_arestas(void)              { return NUM_ARESTAS; }
const ArestaMapa& mapa_aresta(int i)    { return ARESTAS[i]; }
int mapa_num_passagens(void)            { return NUM_PASSAGENS; }
const PassagemMapa& mapa_passagem(int i){ return PASSAGENS[i]; }
int mapa_num_areas(void)                { return NUM_AREAS; }
const AreaMapa& mapa_area(int i)        { return AREAS[i]; }

const AreaMapa* mapa_buscar_area(TipoArea tipo, int numero) {

    for(int i=0;i<NUM_AREAS;i++) {
        if(AREAS[i].tipo == tipo && AREAS[i].numero == numero)
            return &AREAS[i];
    }

    return NULL;
}

const AreaMapa* mapa_area_do_ponto(int x, int y) {

    for(int i=0;i<NUM_AREAS;i++) {
        const AreaMapa& a = AREAS[i];
        if(x >= a.x0 && x <= a.x1 && y >= a.y0 && y <= a.y1)
            return &a;
    }

    return NULL;
}

bool mapa_dentro(int x, int y) {
    return x >= 0 && x <= MAPA_LARGURA && y >= 0 && y <= MAPA_ALTURA;
}

static double distancia(int x0, int y0, int x1, int y1) {
    return std::hypot((double)(x1 - x0), (double)(y1 - y0));
}

int mapa_no_de_acesso(int x, int y) {

    const AreaMapa* area = mapa_area_do_ponto(x, y);

    if(area)
        return area->no_acesso;

    int melhor = 0;
    double melhor_d = std::numeric_limits<double>::max();

    for(int i=0;i<NUM_NOS;i++) {
        double d = distancia(x, y, NOS[i].x, NOS[i].y);
        if(d < melhor_d) {
            melhor_d = d;
            melhor = i;
        }
    }

    return melhor;
}

int mapa_rota(int x0, int y0, int x1, int y1, int tam_equipe, std::vector<Waypoint>& rota) {

    rota.clear();

    int origem = mapa_no_de_acesso(x0, y0);
    int destino = mapa_no_de_acesso(x1, y1);

    // Mesma área / mesmo ponto de acesso: vai direto, sem passar pelo grafo
    if(origem == destino) {
        rota.push_back({x1, y1, -1});
        return 0;
    }

    // Dijkstra (O(V²): o grafo é pequeno). Empates resolvidos pelo menor
    // índice, então robôs da mesma equipe sempre calculam a mesma rota.
    double dist[NUM_NOS];
    int anterior[NUM_NOS];
    int via_passagem[NUM_NOS];
    bool fechado[NUM_NOS];

    for(int i=0;i<NUM_NOS;i++) {
        dist[i] = std::numeric_limits<double>::max();
        anterior[i] = -1;
        via_passagem[i] = -1;
        fechado[i] = false;
    }

    dist[origem] = 0.0;

    while(true) {

        int u = -1;

        for(int i=0;i<NUM_NOS;i++) {
            if(!fechado[i] && dist[i] < std::numeric_limits<double>::max() && (u < 0 || dist[i] < dist[u]))
                u = i;
        }

        if(u < 0 || u == destino)
            break;

        fechado[u] = true;

        for(int e=0;e<NUM_ARESTAS;e++) {

            const ArestaMapa& a = ARESTAS[e];

            if(a.a != u && a.b != u)
                continue;

            if(a.passagem >= 0 && PASSAGENS[a.passagem].capacidade < tam_equipe)
                continue;

            int v = (a.a == u) ? a.b : a.a;
            double d = dist[u] + distancia(NOS[u].x, NOS[u].y, NOS[v].x, NOS[v].y);

            if(d < dist[v]) {
                dist[v] = d;
                anterior[v] = u;
                via_passagem[v] = a.passagem;
            }
        }
    }

    if(anterior[destino] < 0)
        return -1;

    std::vector<int> nos;

    for(int v=destino; v>=0; v=anterior[v])
        nos.push_back(v);

    for(int i=(int)nos.size()-1;i>=0;i--) {
        int v = nos[i];
        rota.push_back({NOS[v].x, NOS[v].y, via_passagem[v]});
    }

    // Sempre inclui o ponto final (mesmo se coincidir com o último nó): robôs
    // de uma equipe com pontos de entrega diferentes têm rotas do mesmo tamanho
    rota.push_back({x1, y1, -1});

    return 0;
}
