#ifndef BAIA_DE_CARGA_HPP
#define BAIA_DE_CARGA_HPP

#include "grafo.hpp"
#include "carga.hpp"
#include <stack>
#include <queue>
#include <deque>
#include "pthread.h"
#include <functional>
#include <mutex>

#define MAX_BC_ROWS 4
#define MAX_BC_COLS 5

// Capacidade da baia: quantas caixas ela guarda ao mesmo tempo (buffer
// limitado do produtor/consumidor). Uma caixa ocupa a vaga desde que entra
// na baia até a missão dela terminar.
#define MAX_CARGAS_BAIA 8


class Baia_carga {

   
    protected:

        int _id;

        int _node_tab[MAX_BC_ROWS];

        int _qtd_node;

        int _rows;

        // Cargas já com pontos na baia, à espera do Organizador. deque (e
        // não queue) para listar_pendentes poder percorrer sem consumir.
        std::deque<Carga> _cargo;

        // Cargas que chegaram e ainda não têm pontos na baia. deque (e não
        // queue) para pub_cargo poder escolher a maior, não só a primeira.
        std::deque<Carga> _load;

        // Vagas livres do buffer (MAX_CARGAS_BAIA). add_cargo consome uma
        // vaga, liberar_vaga a devolve quando a missão termina.
        sem_t bc_sem;

        sem_t arive_sem;
        
        sem_t cargo_wait_sem;

        std::mutex m;

        sem_t avaiable_sem;

        // Maior caixa que a frota atende agora (definida pelo main). Sem
        // isso a baia escolhia uma caixa de 4 numa frota de 3 e ficava presa
        // nela, com as menores esperando atrás.
        std::function<int(void)> _capacidade_frota;

    public:

        std::vector<Node*> _node;

        Baia_carga(int id, int rows, std::vector<Node*>& node);

        int get_id(void);
        
        // Acrescenta uma caixa nova se houver vaga. Nunca bloqueia (quem chama
        // é a thread de comandos da ponte MQTT): retorna -1 com a baia cheia.
        int add_cargo(Carga cargo);

        // Uma caixa saiu da baia de vez (entregue, ou descartada no
        // encerramento): devolve a vaga dela ao buffer.
        void liberar_vaga(void);

        // Caixa de uma missão que falhou volta para a fila. Ela não chegou a
        // liberar a vaga, então não passa pelo controle de capacidade.
        void devolver_cargo(Carga cargo);

        void definir_capacidade_frota(std::function<int(void)> capacidade);

        int pub_cargo(void);

        std::vector<Node*> get_avaiable_nodes(int type);

        int free_ocupied_nodes(std::vector<Node*>& nodes);
        
        Carga cargo_request_unload(void);

        // Tamanhos das cargas ainda na baia (_load e _cargo), sem consumi-las.
        // Para a ponte MQTT publicar a fila de cada baia.
        void listar_pendentes(std::vector<int>& tamanhos);

        
};

#endif