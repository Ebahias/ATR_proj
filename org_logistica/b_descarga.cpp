#include "b_descarga.hpp"


Baia_descarga::Baia_descarga(int id,int rows, std::vector<Node*>& node) {

    this->_id = id;
    this->_rows = rows;

    for(auto v : node) {
        this->_node.push_back(v);
    }

    memset(_node_ocupied, 0, _rows*sizeof(bool));

    // Inicia um semáforo
    sem_init(&this->bc_sem, 0, rows);
    sem_init(&avb_sem, 0, rows);

    sem_init(&avaiable_sem, 0, rows);

}

Node* Baia_descarga::receive_cargo(int type) {

    if(type > _qtd_node) {
        return nullptr;
    }

    sem_wait(&avb_sem);

    Node* node = this->get_free_spot();

    return node;
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
        sem_wait(&this->bc_sem);
    }
    
    
    for(int i=0;i<cargo.get_type();i++) {

        if(!this->_node_ocupied[i]) {
            cargo.set_node_id_on(this->_node[i]);
            this->_node_ocupied[i] = true;
        }

    }
    
    _cargo.push(cargo);

    return 0;
}

Carga Baia_descarga::cargo_response_clear(void) {
    return _cargo.front();
}

Node* Baia_descarga::get_free_spot(void) {

    Node* node = nullptr;

    m.lock();

    for(int i=0;i<_qtd_node;i++) {
        if(!_node_ocupied[i]) {
            node = _node[i];
            _node_ocupied[i] = true;
        }
    }

    m.unlock();

    return node;
}

// Reserva "type" pontos livres da zona, todos ou nenhum. Procurar e travar
// precisam estar na mesma seção crítica: antes, entre soltar e pegar o mutex
// de novo, duas missões podiam escolher os mesmos pontos. Também reserva só
// os "type" pontos pedidos, e não todos os livres da zona.
std::vector<Node*> Baia_descarga::get_avaiable_spots(int type) {

    std::vector<Node*> ret;

    std::lock_guard<std::mutex> lock(m);

    for(auto v : _node) {

        if((int)ret.size() == type)
            break;

        if(!v->is_locked()) {

            ret.push_back(v);
        }

    }

    if((int)ret.size() < type) {
        ret.clear();
        return ret;
    }

    for(auto v : ret) {

        v->set_locked();

    }

    return ret;
}

int Baia_descarga::free_occupied_spots(std::vector<Node*> nodes) {

     std::lock_guard<std::mutex> lock(m);

    for(auto v : nodes) {

        v->set_free();

    }

    return nodes.size();
}

int Baia_descarga::free_node(Node* node) {

    for(int i=0;i<_qtd_node;i++) {

        if(node == _node[i]) {
            m.lock();
            _node_ocupied[i] = false;
            m.unlock();
            break;
        }
    }

    return _qtd_node;
}
