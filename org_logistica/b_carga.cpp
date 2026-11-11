#include "b_carga.hpp"
#include <cstring>
#include "comum/log.hpp"

Baia_carga::Baia_carga(int id, int rows, std::vector<Node*>& node) {

    this->_id = id;
    this->_rows = rows;

    for(auto v : node) {
        this->_node.push_back(v);
    }
    
    std::memset(_node_ocupied, 0, _rows*sizeof(bool));

    // Inicia um semáforo
    sem_init(&this->bc_sem, 0, rows);

    sem_init(&this->arive_sem, 0, 0);

    sem_init(&this->cargo_wait_sem, 0, 0);

    sem_init(&this->avaiable_sem, 0, rows);
}

int Baia_carga::get_id(void) {

    return _id;
}

int Baia_carga::add_cargo(Carga cargo) {

    m.lock();

    this->_load.push(cargo);

    m.unlock();

    sem_post(&this->arive_sem);

    return 0;
}

int Baia_carga::get_node_idx(int node_id) {

    int ret = -1;

    for(int i=0;i<_rows;i++) {
        if(node_id == _node[i]->get_id())
            ret = i;
    }

    return ret;
}

Carga Baia_carga::consume_cargo(void) {


    for(int i=0;i<_cargo.front().get_type();i++) {
        
        int idx = this->get_node_idx(_cargo.front()._node_id[i]);
        
        if(idx >= 0)
            this->_node_ocupied[idx] = false;
        
        sem_post(&this->bc_sem);
    }

    Carga cargo_ret = _cargo.front();

    _cargo.pop();

    return cargo_ret;
}

int Baia_carga::pub_cargo(void) {

    sem_wait(&this->arive_sem);
    
    if(_load.empty())
        return -1;

    m.lock();

        Carga cargo = _load.front();

    m.unlock();

    std::vector<Node*> av_node = this->get_avaiable_nodes(cargo.get_type());

    m.lock();

    _load.pop();

    m.unlock();

    for(auto v : av_node) {
        
        cargo._node.push_back(v);

    }

    if(cargo._node.empty())
        return -2;
    
    m.lock();

    _cargo.push(cargo);

    m.unlock();

    log_msg("B CARGA", "CARGA A DISPOSICAO NA POSICAO <%f, %f>", cargo._node[0]->get_pos_x(), cargo._node[0]->get_pos_y());
    
    sem_post(&cargo_wait_sem);

    return 0;
}

Carga Baia_carga::cargo_request_unload(void) {

    sem_wait(&cargo_wait_sem);

    Carga ret = _cargo.back();

    _cargo.pop();

    return ret;
}

std::vector<Node*> Baia_carga::get_avaiable_nodes(int type) {

    for(int i=0;i<type;i++) 
        sem_wait(&this->avaiable_sem);

    
    int qtd_node = 0;

    std::vector<Node*> ret;

    for(auto v : _node) {

        if(!v->is_locked()) {

            ret.push_back(v);

            v->set_locked();
            qtd_node++;        
        }
        if(qtd_node == type)
            break;
    }


    return ret;
}

int Baia_carga::free_ocupied_nodes(std::vector<Node*>& nodes) {

    int qtd_nodes = 0;

    for(auto v : nodes) {

        v->set_free();
        log_msg("BAIA CARGA", "NÓ <%f, %f> LIBERADO", v->get_pos_x(), v->get_pos_y());
        qtd_nodes++;
        sem_post(&this->avaiable_sem);

    }

    return qtd_nodes;
}
