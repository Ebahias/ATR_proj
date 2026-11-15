#include "b_carga.hpp"
#include <cstring>
#include "comum/log.hpp"

Baia_carga::Baia_carga(int id, int rows, std::vector<Node*>& node) {

    this->_id = id;
    this->_rows = rows;

    for(auto v : node) {
        this->_node.push_back(v);
    }

    // Vagas do buffer limitado (antes iniciado com rows e nunca esperado:
    // a fila não tinha limite)
    sem_init(&this->bc_sem, 0, MAX_CARGAS_BAIA);

    sem_init(&this->arive_sem, 0, 0);

    sem_init(&this->cargo_wait_sem, 0, 0);

    sem_init(&this->avaiable_sem, 0, rows);
}

int Baia_carga::get_id(void) {

    return _id;
}

int Baia_carga::add_cargo(Carga cargo) {

    // sem_trywait e não sem_wait: com a baia cheia a caixa é recusada na
    // hora, em vez de travar quem a está acrescentando
    if(sem_trywait(&this->bc_sem) != 0)
        return -1;

    m.lock();

    this->_load.push_back(cargo);

    m.unlock();

    sem_post(&this->arive_sem);

    return 0;
}

void Baia_carga::liberar_vaga(void) {

    sem_post(&this->bc_sem);
}

void Baia_carga::devolver_cargo(Carga cargo) {

    m.lock();

    this->_load.push_back(cargo);

    m.unlock();

    sem_post(&this->arive_sem);
}

void Baia_carga::definir_capacidade_frota(std::function<int(void)> capacidade) {

    _capacidade_frota = capacidade;
}

int Baia_carga::pub_cargo(void) {

    sem_wait(&this->arive_sem);

    // Com nenhuma caixa que a frota atenda, espera a frota crescer (ou
    // chegar uma caixa menor), revendo a cada intervalo
    struct timespec espera = { 0, 500 * 1000000L };
    bool avisou = false;
    size_t escolhida = 0;

    while(true) {

        // Fora do mutex da baia: consulta o manager, que tem o seu mutex
        int limite = _capacidade_frota ? _capacidade_frota() : 4;

        // add_cargo e devolver_cargo fazem push em _load ao mesmo tempo, de
        // outras threads: a consulta precisa do mutex tanto quanto o front().
        m.lock();

        if(_load.empty()) {
            m.unlock();
            return -1;
        }

        // Prioridade para a maior carga que a frota atende; entre as de
        // mesmo tamanho, a mais antiga. As grandes precisam de mais robôs
        // livres ao mesmo tempo e, por ordem de chegada, ficavam esperando
        // atrás das pequenas.
        bool achou = false;

        for(size_t i=0;i<_load.size();i++) {
            if(_load[i].get_type() <= limite && (!achou || _load[i].get_type() > _load[escolhida].get_type())) {
                escolhida = i;
                achou = true;
            }
        }

        if(achou)
            break;

        m.unlock();

        if(!avisou) {
            log_msg("B CARGA", "BAIA %d: NENHUMA CAIXA CABE NA FROTA ATUAL (%d ROBÔS EM AUTOMÁTICO); AGUARDANDO ROBÔS", _id, limite);
            avisou = true;
        }

        nanosleep(&espera, NULL);
    }

        Carga cargo = _load[escolhida];

    m.unlock();

    std::vector<Node*> av_node = this->get_avaiable_nodes(cargo.get_type());

    // Só esta thread remove de _load; add_cargo e devolver_cargo só
    // acrescentam no fim: o índice escolhido continua na mesma carga
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

    // A espera pelos pontos fica fora do mutex: bloquear segurando-o travaria
    // add_cargo e listar_pendentes
    for(int i=0;i<type;i++) 
        sem_wait(&this->avaiable_sem);

    
    int qtd_node = 0;

    std::vector<Node*> ret;

    // Procura e reserva na mesma seção crítica: o par is_locked + set_locked
    // não é atômico, e duas reservas intercaladas pegariam o mesmo ponto
    std::lock_guard<std::mutex> lock(m);

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
