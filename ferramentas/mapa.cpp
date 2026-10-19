/*
 * Consulta o mapa do galpão (comum/mapa.hpp).
 *
 * Uso: mapa json                              exporta o mapa (para o simulador/GUI da Etapa 2)
 *      mapa rota <x0> <y0> <x1> <y1> [equipe] imprime a rota calculada
 */

#include "comum/mapa.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void exportar_json(void) {

    printf("{\n  \"largura\": %d,\n  \"altura\": %d,\n", MAPA_LARGURA, MAPA_ALTURA);

    printf("  \"nos\": [\n");
    for(int i=0;i<mapa_num_nos();i++) {
        const NoMapa& n = mapa_no(i);
        printf("    {\"id\": %d, \"nome\": \"%s\", \"x\": %d, \"y\": %d}%s\n",
               i, n.nome, n.x, n.y, i + 1 < mapa_num_nos() ? "," : "");
    }

    printf("  ],\n  \"arestas\": [\n");
    for(int i=0;i<mapa_num_arestas();i++) {
        const ArestaMapa& a = mapa_aresta(i);
        printf("    {\"a\": %d, \"b\": %d, \"passagem\": %d}%s\n",
               a.a, a.b, a.passagem, i + 1 < mapa_num_arestas() ? "," : "");
    }

    printf("  ],\n  \"passagens\": [\n");
    for(int i=0;i<mapa_num_passagens();i++) {
        const PassagemMapa& p = mapa_passagem(i);
        printf("    {\"id\": %d, \"nome\": \"%s\", \"capacidade\": %d, \"a\": %d, \"b\": %d}%s\n",
               i, p.nome, p.capacidade, p.a, p.b, i + 1 < mapa_num_passagens() ? "," : "");
    }

    static const char* tipos[] = {"baia", "zona", "recarga"};

    printf("  ],\n  \"areas\": [\n");
    for(int i=0;i<mapa_num_areas();i++) {
        const AreaMapa& a = mapa_area(i);
        printf("    {\"tipo\": \"%s\", \"numero\": %d, \"nome\": \"%s\", \"x0\": %d, \"y0\": %d, "
               "\"x1\": %d, \"y1\": %d, \"no_acesso\": %d, \"cx\": %d, \"cy\": %d}%s\n",
               tipos[a.tipo], a.numero, a.nome, a.x0, a.y0, a.x1, a.y1, a.no_acesso, a.cx, a.cy,
               i + 1 < mapa_num_areas() ? "," : "");
    }

    printf("  ],\n  \"vagas_recarga\": [\n");
    for(int i=0;i<mapa_num_vagas_recarga();i++) {
        const VagaRecargaMapa& v = mapa_vaga_recarga(i);
        printf("    {\"id\": %d, \"recarga\": %d, \"x\": %d, \"y\": %d}%s\n",
               i, v.recarga, v.x, v.y, i + 1 < mapa_num_vagas_recarga() ? "," : "");
    }

    printf("  ]\n}\n");
}

static int imprimir_rota(int x0, int y0, int x1, int y1, int equipe) {

    std::vector<Waypoint> rota;

    if(mapa_rota(x0, y0, x1, y1, equipe, rota) < 0) {
        printf("sem rota para equipe de %d\n", equipe);
        return 1;
    }

    printf("(%d,%d)", x0, y0);

    for(const Waypoint& w : rota) {
        if(w.passagem >= 0)
            printf(" =[P%d cap %d]=> (%d,%d)", w.passagem, mapa_passagem(w.passagem).capacidade, w.x, w.y);
        else
            printf(" -> (%d,%d)", w.x, w.y);
    }

    printf("\n");

    return 0;
}

int main(int argc, char** argv) {

    if(argc == 2 && strcmp(argv[1], "json") == 0) {
        exportar_json();
        return 0;
    }

    if((argc == 6 || argc == 7) && strcmp(argv[1], "rota") == 0) {
        return imprimir_rota(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                             argc == 7 ? atoi(argv[6]) : 1);
    }

    fprintf(stderr, "uso: %s json | rota <x0> <y0> <x1> <y1> [equipe]\n", argv[0]);
    return 2;
}
