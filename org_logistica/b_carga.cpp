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

    this->_load.push_back(cargo);

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

    _cargo.pop_front();

    return cargo_ret;
}

int Baia_carga::pub_cargo(void) {

    sem_wait(&this->arive_sem);

    // A thread de chegada de cargas faz push em _load ao mesmo tempo: a
    // consulta precisa do mutex tanto quanto o front().
    m.lock();

    if(_load.empty()) {
        m.unlock();
        return -1;
    }

    // Prioridade para a maior carga; entre as de mesmo tamanho, a mais
    // antiga. As grandes precisam de mais robôs livres ao mesmo tempo e, por
    // ordem de chegada, ficavam esperando atrás das pequenas.
    size_t escolhida = 0;

    for(size_t i=1;i<_load.size();i++) {
        if(_load[i].get_type() > _load[escolhida].get_type())
            escolhida = i;
    }

        Carga cargo = _load[escolhida];

    m.unlock();

    std::vector<Node*> av_node = this->get_avaiable_nodes(cargo.get_type());

    // Só esta thread remove de _load e a de chegada só acrescenta no fim:
    // o índice escolhido continua apontando para a mesma carga
    m.lock();

    _load.erase(_load.begin() + escolhida);

    m.unlock();

    for(auto v : av_node) {
        
        cargo._node.push_back(v);

    }

    if(cargo._node.empty())
        return -2;
    
    m.lock();

    _cargo.push_back(cargo);

    m.unlock();

    log_msg("B CARGA", "CARGA A DISPOSICAO NA POSICAO <%f, %f>", cargo._node[0]->get_pos_x(), cargo._node[0]->get_pos_y());
    
    sem_post(&cargo_wait_sem);

    return 0;
}

Carga Baia_carga::cargo_request_unload(void) {

    sem_wait(&cargo_wait_sem);

    // front(), não back(): pop() remove o da frente. Com back() a carga
    // devolvida era a mais nova e a removida, a mais antiga (que se perdia).
    m.lock();

    Carga ret = _cargo.front();

    _cargo.pop_front();

    m.unlock();

    return ret;
}

void Baia_carga::listar_pendentes(std::vector<int>& tamanhos) {

    std::lock_guard<std::mutex> lock(m);

    tamanhos.clear();

    for(auto& c : _cargo)
        tamanhos.push_back(c.get_type());

    for(auto& c : _load)
        tamanhos.push_back(c.get_type());
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
