#ifndef BAIA_DE_CARGA_HPP
#define BAIA_DE_CARGA_HPP

#include "grafo.hpp"
#include "carga.hpp"
#include <stack>
#include <queue>
#include <deque>
#include "pthread.h"
#include <mutex>

#define MAX_BC_ROWS 4
#define MAX_BC_COLS 5


class Baia_carga {

   
    protected:

        int _id;

        bool _node_ocupied[MAX_BC_ROWS];

        int _node_tab[MAX_BC_ROWS];

        int _qtd_node;

        int _rows;

        // Cargas já com pontos na baia, à espera do Organizador. deque (e
        // não queue) para listar_pendentes poder percorrer sem consumir.
        std::deque<Carga> _cargo;

        // Cargas que chegaram e ainda não têm pontos na baia. deque (e não
        // queue) para pub_cargo poder escolher a maior, não só a primeira.
        std::deque<Carga> _load;

        sem_t bc_sem;

        sem_t arive_sem;
        
        sem_t cargo_wait_sem;

        std::mutex m;

        int get_node_idx(int node_id);

        sem_t avaiable_sem;

    public:

        std::vector<Node*> _node;

        Baia_carga(int id, int rows, std::vector<Node*>& node);

        int get_id(void);
        
        int add_cargo(Carga cargo);

        Carga consume_cargo(void);

        int pub_cargo(void);

        std::vector<Node*> get_avaiable_nodes(int type);

        int free_ocupied_nodes(std::vector<Node*>& nodes);
        
        Carga cargo_request_unload(void);

        // Tamanhos das cargas ainda na baia (_load e _cargo), sem consumi-las.
        // Para a ponte MQTT publicar a fila de cada baia.
        void listar_pendentes(std::vector<int>& tamanhos);

        
};

#endif