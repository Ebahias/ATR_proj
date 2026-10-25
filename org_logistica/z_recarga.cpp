#include "z_recarga.hpp"


Zona_recarga::Zona_recarga(int id, int rows, Node** nodes) {

    this->_id = id;
    this->_rows = rows;

    for(int i=0;i<this->_rows;i++) {
        this->_node[i] = nodes[i];
    }

    std::memset(_node_ocupied, 0, _rows*sizeof(bool));

    sem_init(&zone_sem, 0, rows);

}

Node* Zona_recarga::get_free_spot(void) {


    Node* ret = nullptr;

    m.lock();

    
    for(int i=0;i<_rows;i++) {
        if(!_node_ocupied[i])
            ret = _node[i];
    }

    m.unlock();

    return ret;
}
        
int Zona_recarga::reserve_spot(int node_id) {

    int idx = this->get_node_idx(node_id);

    if(idx < 0)
        return -1;
    
    m.lock();

    this->_node_ocupied[idx] = true;

    m.unlock();

    return idx;
}

int Zona_recarga::free_spot(int node_id) {

    int idx = this->get_node_idx(node_id);
    
    if(idx < 0)
        return -1;

    m.lock();

    this->_node_ocupied[idx] = false;

    m.unlock();

    sem_post(&this->zone_sem);

    return idx;
}

int Zona_recarga::get_node_idx(int node_id) {

    int ret = -1;

    for(int i=0;i<_rows;i++) {
        if(node_id == _node[i]->get_id()) 
            ret = i;
    }

    return ret;
}

void Zona_recarga::wait_for_spot(void) {
    sem_wait(&this->zone_sem);
}
