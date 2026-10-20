#ifndef BAIA_DE_CARGA_HPP
#define BAIA_DE_CARGA_HPP

#include "grafo.hpp"
#include "carga.hpp"
#include <stack>
#include <queue>
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

        std::queue<Carga> _cargo;

        std::queue<Carga> _load;

        sem_t bc_sem;

        sem_t arive_sem;
        
        sem_t cargo_wait_sem;

        std::mutex m;

        int get_node_idx(int node_id);

    public:

        Node* _node[MAX_BC_ROWS];

        Baia_carga(int id, int rows, Node** nodes);

        int add_cargo(Carga cargo);

        Carga consume_cargo(void);

        int pub_cargo(void);

        Carga cargo_request_unload(void);
        
        
};

#endif