#include "b_descarga.hpp"


Baia_descarga::Baia_descarga(int id,int rows, Node** nodes) {

    this->_id = id;
    this->_rows = rows;

    for(int i=0;i<this->_rows;i++) {
        this->_node[i] = nodes[i];
    }

    memset(_node_ocupied, 0, _rows*sizeof(bool));

    // Inicia um semáforo
    sem_init(&this->bc_sem, 0, rows);

}

int Baia_descarga::receive_cargo(Carga cargo) {

    m.lock();

    this->_load.push(cargo);

    m.unlock();

    return 0;
}

int Baia_descarga::get_node_idx(int node_id) {

    int ret = -1;

    for(int i=0;i<_rows;i++) {
        if(node_id == _node[i]->get_id())
            ret = i;
    }

    return ret;
}

int Baia_descarga::consume_cargo(void) {


    for(int i=0;i<_cargo.front().get_type();i++) {
        
        int idx = this->get_node_idx(_cargo.front()._node_id[i]);
        
        if(idx >= 0)
            this->_node_ocupied[idx] = false;
        
        sem_post(&this->bc_sem);
    }

    _cargo.pop();

    return 0;
}

int Baia_descarga::pub_cargo(void) {

    m.lock();

    Carga cargo = _load.front();

    m.unlock();

    _load.pop();

    // espera ter as vagas necessárias para adicionar na espera
    for(int i=0;i<cargo.get_type();i++) {
        sem_wait(%this->bc_sem);
    }
    
    
    for(int i=0;i<cargo.get_type();i++) {

        if(!this->_node_ocupied[i]) {
            cargo->set_node_id_on(this->_node[i]->get_id());
            this->_node_ocupied[i] = true;
        }

    }
    
    _cargo.push(cargo);

    return 0;
}

Carga Baia_descarga::cargo_response_clear(void) {
    return _cargo.front();
}