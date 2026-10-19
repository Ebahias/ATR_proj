#ifndef COMUM_MAPA_HPP
#define COMUM_MAPA_HPP

#include <vector>

/*
 * Mapa do galpão (vista superior, y cresce para baixo), compartilhado por
 * todos os processos. É um grafo de pontos de passagem (nós) ligados por
 * trechos (arestas). Alguns trechos são passagens estreitas, com capacidade
 * para 1, 2 ou 3 robôs ao mesmo tempo.
 *
 *  x:  0      30  50  70        100       130  150  170     200
 *   15 |Baia 1| B1--W1---------- N ----------------E1--ZA |Zona A|
 *      |      |     |            |                  |      |      |
 *   30 |      |     |        R1 (recarga 1, 3 vagas) |      |      |
 *   45 |Baia 2| B2--W2--P0a ==[cap 1]== P0b--------E2--ZB |Zona B|
 *      |      |     ‖ [cap 3]                       |      |      |
 *   75 |Baia 3| B3--W3--P1a ==[cap 2]== P1b--------E3--ZC |Zona C|
 *      |      |     |                               |      |      |
 *   90 |      |     |        R2 (recarga 2, 3 vagas) |      |      |
 *  105 |Baia 4| B4--W4---------- S ----------------E4--ZD |Zona D|
 *
 * Os corredores norte (y=15), sul (y=105) e leste (x=150) são largos, então
 * sempre existe rota para uma equipe de 4 robôs.
 */

#define MAPA_LARGURA    200
#define MAPA_ALTURA     120

#define MAX_PASSAGENS   8

#define MAX_VAGAS_RECARGA   8

struct NoMapa {
    const char* nome;
    int x;
    int y;
};

struct ArestaMapa {
    int a;
    int b;
    int passagem;               // índice da passagem estreita, -1 se livre
};

struct PassagemMapa {
    const char* nome;
    int capacidade;             // robôs ao mesmo tempo
    int a;                      // nós das extremidades
    int b;
};

enum TipoArea : int {
    AREA_BAIA = 0,
    AREA_ZONA,
    AREA_RECARGA
};

struct AreaMapa {
    TipoArea tipo;
    int numero;                 // Baia 1..4, Zona 1..4 (A..D), Recarga 1..2
    const char* nome;
    int x0, y0, x1, y1;         // retângulo da área
    int no_acesso;              // nó do grafo por onde se entra na área
    int cx, cy;                 // centro (ponto padrão de coleta/entrega)
};

// Vaga (ponto de encaixe) de uma estação de recarga
struct VagaRecargaMapa {
    int recarga;                // número da área de recarga
    int x;
    int y;
};

// Ponto da rota. "passagem" é a passagem estreita atravessada para chegar a
// este ponto a partir do anterior (-1 se o trecho é livre).
struct Waypoint {
    int x;
    int y;
    int passagem;
};

int mapa_num_nos(void);
const NoMapa& mapa_no(int i);

int mapa_num_arestas(void);
const ArestaMapa& mapa_aresta(int i);

int mapa_num_passagens(void);
const PassagemMapa& mapa_passagem(int i);

int mapa_num_areas(void);
const AreaMapa& mapa_area(int i);

int mapa_num_vagas_recarga(void);
const VagaRecargaMapa& mapa_vaga_recarga(int i);

// Área do tipo/número pedido, ou NULL.
const AreaMapa* mapa_buscar_area(TipoArea tipo, int numero);

// Área que contém o ponto, ou NULL.
const AreaMapa* mapa_area_do_ponto(int x, int y);

bool mapa_dentro(int x, int y);

// Nó por onde o ponto entra no grafo: o nó de acesso da área que o contém
// ou, fora das áreas, o nó mais próximo.
int mapa_no_de_acesso(int x, int y);

/*
 * Rota de (x0, y0) até (x1, y1) pelo menor caminho (Dijkstra).
 * tam_equipe: número de robôs que precisam atravessar juntos (equipe
 * carregando uma caixa); passagens com capacidade menor são evitadas.
 * Retorna 0 e preenche "rota" (sem o ponto de partida), ou -1 se não há rota.
 */
int mapa_rota(int x0, int y0, int x1, int y1, int tam_equipe, std::vector<Waypoint>& rota);

#endif
