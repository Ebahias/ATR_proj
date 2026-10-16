#ifndef BAIA_DE_DESCARGA_HPP
#define BAIA_DE_DESCARGA_HPP

#include "grafo.hpp"
#include "carga.hpp"
#include <stack>

#define MAX_BD_ROWS 4

class Baia_descarga {

   protected:

        int _id;

        bool _node_ocupied[MAX_BD_ROWS];

        int _node_tab[MAX_BD_ROWS];

        int _qtd_node;

        int _rows;

        std::queue<Carga> _cargo;

        std::queue<Carga> _load;

        sem_t bc_sem[4];

        std::mutex m;

        int get_node_idx(int node_id);

    public:

        Node* _node[MAX_BC_ROWS];

        Baia_descarga(int id, int rows, Node** nodes);

        int consume_cargo(void);

        int receive_cargo(void);

        int pub_cargo(void);

        Carga cargo_response_clear(void);
        
};

#endif