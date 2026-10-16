#ifndef CARGA_HPP
#define CARGA_HPP

#include "grafo.hpp"

class Carga {

    protected:

        int _type;
        
        int _seted_nodes;
    public:

        int _node_id[4];

        Carga(int type);

        int get_type(void);

        int set_node_id_on(int node_id);
};

#endif