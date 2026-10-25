#include "carga.hpp"

Carga::Carga(int type) {

    this->_type = type;
    this->_seted_nodes = 0;    
}

int Carga::get_type(void) {

    return this->_type;
}

int Carga::set_node_id_on(Node* node) {

    if(this->_seted_nodes == _type) {
        return -1;
    }

    _node.push_back(node);
    
    _seted_nodes++;

    return _seted_nodes;
}

