#ifndef ZONA_DE_RECARGA_HPP
#define ZONA_DE_RECARGA_HPP

#include "grafo.hpp"
#include <mutex>
#include "pthreads.hpp"

#define MAX_ZONE_ROWS 6


class Zona_recarga {

   protected:

        int _id;

        bool _node_ocupied[MAX_ZONE_ROWS];

        int _node_tab[MAX_ZONE_ROWS];

        int _qtd_node;

        int _rows;

        sem_t zone_sem[MAX_ZONE_ROWS];

        std::mutex m;

        int get_node_idx(int node_id);

    public:

        Node* _node[MAX_ZONE_ROWS];

        Zona_recarga(int id, int rows, Node** nodes);

        int get_free_spot(void);
        
        int reserve_spot(int node_id);

        int free_spot(int node_id);

        void wait_for_spot(void);
};

#endif