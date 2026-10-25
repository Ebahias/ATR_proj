#ifndef CARGA_HPP
#define CARGA_HPP

#include "grafo.hpp"
#include <cstring>

class Carga {

    protected:

        int _type;
        
        int _seted_nodes;
    public:

        int _node_id[4];
        
        std::vector<Node*> _node;

        Carga(int type);

        int get_type(void);

        int set_node_id_on(Node* node);

};

#endif