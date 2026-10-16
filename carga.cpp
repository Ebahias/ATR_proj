#include "carga.hpp"

Carga::Carga(int type) {

    this->_type = type;
    this->_seted_nodes = 0;
    memset(this->_node, 0, 4*sizeof(int));
    
}

int Carga::get_type(void) {

    return this->_type;
}

int Carga::set_node_id_on(int node_id) {

    if(this->_seted_nodes == _type) {
        return -1;
    }

    _node_id[_seted_nodes] = node_id;

    _seted_nodes++;

    return _seted_nodes;
}

