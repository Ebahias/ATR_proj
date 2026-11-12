#include "robo_manager.hpp"
#include "despacho.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <time.h>
#include <utility>

Robo_manager::Robo_manager(std::vector<Baia_carga*>& b_carga, std::vector<Baia_descarga*>& b_descarga, std::vector<Zona_recarga*>& z_recarga) {

    _b_carga = b_carga;
    _b_descarga = b_descarga;
    _z_recarga = z_recarga;

    // O robô recusa CMD_TAREFA com tarefa_id <= 0
    _num_tarefas = 1;

    _entregues = 0;
    _encerrando = false;

    pthread_mutex_init(&_mtx_missoes, NULL);
    pthread_cond_init(&_cv_missoes, NULL);
}

Robo_manager::~Robo_manager() {

    pthread_cond_destroy(&_cv_missoes);
    pthread_mutex_destroy(&_mtx_missoes);
}

// Mesmas condições que o robô confere antes de aceitar um CMD_TAREFA do
// Organizador (robo/navegacao.cpp): sem isso a tarefa seria ignorada e a
// missão só descobriria no timeout.
bool Robo_manager::pode_receber_tarefa(Robo* robo, DadosStatus& d) {

    if(robo->_removido || robo->_reservado)
        return false;

    if(!despacho_ler_status(robo->get_id(), d))
        return false;

    return d.modo == MODO_AUTOMATICO
        && d.estado == ESTADO_DISPONIVEL
        && !d.falha
        && (d.fase == FASE_NENHUMA || d.fase == FASE_CONCLUIDA || d.fase == FASE_FALHOU);
}

Robo* Robo_manager::escolher_disponivel(float ref_x, float ref_y) {

    Robo* melhor = nullptr;
    float melhor_dist = 0;

    for(auto r : _robo) {

        DadosStatus d;

        if(!pode_receber_tarefa(r, d))
            continue;

        float dist = ref_x < 0 ? 0 : (float)hypot(d.pos_x - ref_x, d.pos_y - ref_y);

        if(melhor == nullptr || dist < melhor_dist) {
            melhor = r;
            melhor_dist = dist;
        }
    }

    if(melhor != nullptr) {
        melhor->_reservado = true;
        melhor->_status = GOING_TO_BAIA_CARGA;
    }

    return melhor;
}

Robo* Robo_manager::get_avaiable_robo(float ref_x, float ref_y) {

    std::lock_guard<std::mutex> lock(m);

    return escolher_disponivel(ref_x, ref_y);
}

// Maior pedido que a frota consegue atender: o tamanho da equipe não pode
// passar do número de robôs em modo automático. Robôs recarregando, com
// defeito ou em outra missão contam, porque voltam a ficar disponíveis.
int Robo_manager::maior_pedido_atendivel(void) {

    int capacidade = 0;

    for(auto r : _robo) {

        DadosStatus d;

        if(!r->_removido && despacho_ler_status(r->get_id(), d) && d.modo == MODO_AUTOMATICO)
            capacidade++;
    }

    int maior = 0;

    for(auto& p : _pedidos) {
        if(p.second <= capacidade && p.second > maior)
            maior = p.second;
    }

    return maior;
}

void Robo_manager::registrar_pedido(int baia, int n) {

    std::lock_guard<std::mutex> lock(m);

    _pedidos.push_back(std::make_pair(baia, n));
}

void Robo_manager::retirar_pedido(int baia, int n) {

    std::lock_guard<std::mutex> lock(m);

    auto it = std::find(_pedidos.begin(), _pedidos.end(), std::make_pair(baia, n));

    if(it != _pedidos.end())
        _pedidos.erase(it);
}

bool Robo_manager::reservar_equipe(int n, float ref_x, float ref_y, std::vector<Robo*>& equipe) {

    std::lock_guard<std::mutex> lock(m);

    equipe.clear();

    // Cede a vez para uma carga maior que esteja esperando
    if(n < maior_pedido_atendivel())
        return false;

    for(int i=0;i<n;i++) {

        Robo* r = escolher_disponivel(ref_x, ref_y);

        if(r == nullptr)
            break;

        equipe.push_back(r);
    }

    if((int)equipe.size() == n)
        return true;

    // Não deu para montar a equipe toda: devolve os que pegou
    for(auto r : equipe) {
        r->_reservado = false;
        r->_status = AVAIABLE;
    }

    equipe.clear();

    return false;
}

void Robo_manager::liberar_robo(Robo* robo) {

    std::lock_guard<std::mutex> lock(m);

    robo->_reservado = false;
    robo->_status = AVAIABLE;
}

void Robo_manager::remover_robo(int robo_id) {

    std::lock_guard<std::mutex> lock(m);

    for(auto r : _robo) {
        if(r->get_id() == robo_id)
            r->_removido = true;
    }
}

int Robo_manager::num_robos_ativos(void) {

    std::lock_guard<std::mutex> lock(m);

    int n = 0;

    for(auto r : _robo) {
        if(!r->_removido)
            n++;
    }

    return n;
}

void Robo_manager::missao_iniciou(const MissaoInfo& missao) {

    pthread_mutex_lock(&_mtx_missoes);
    _missoes[missao.tarefa] = missao;
    pthread_mutex_unlock(&_mtx_missoes);
}

void Robo_manager::missao_definir_zona(int tarefa, int zona) {

    pthread_mutex_lock(&_mtx_missoes);

    auto it = _missoes.find(tarefa);

    if(it != _missoes.end())
        it->second.zona = zona;

    pthread_mutex_unlock(&_mtx_missoes);
}

void Robo_manager::missao_terminou(int tarefa, bool entregue) {

    pthread_mutex_lock(&_mtx_missoes);
    _missoes.erase(tarefa);
    if(entregue)
        _entregues++;
    pthread_cond_broadcast(&_cv_missoes);
    pthread_mutex_unlock(&_mtx_missoes);
}

// Cada parte é copiada sob o seu mutex (pedidos com m, filas com o mutex da
// baia, missões com _mtx_missoes), nunca dois ao mesmo tempo: não cria uma
// ordem de travamento nova entre eles. Uma carga que acabou de passar de uma
// parte para a outra pode aparecer na publicação seguinte, não nesta.
EstadoCargas Robo_manager::estado_cargas(void) {

    EstadoCargas e;

    e.pendentes.resize(_b_carga.size());

    for(size_t b=0;b<_b_carga.size();b++)
        _b_carga[b]->listar_pendentes(e.pendentes[b]);

    {
        std::lock_guard<std::mutex> lock(m);

        // A carga que a thread da baia já tirou da fila e está esperando
        // equipe continua pendente
        for(auto& p : _pedidos) {
            if(p.first >= 0 && p.first < (int)e.pendentes.size())
                e.pendentes[p.first].insert(e.pendentes[p.first].begin(), p.second);
        }
    }

    pthread_mutex_lock(&_mtx_missoes);

    for(auto& par : _missoes)
        e.em_transporte.push_back(par.second);

    e.entregues = _entregues;

    pthread_mutex_unlock(&_mtx_missoes);

    return e;
}

int Robo_manager::entregues(void) {

    pthread_mutex_lock(&_mtx_missoes);
    int n = _entregues;
    pthread_mutex_unlock(&_mtx_missoes);

    return n;
}

bool Robo_manager::esperar_missoes(int timeout_ms) {

    struct timespec limite;
    clock_gettime(CLOCK_REALTIME, &limite);
    limite.tv_sec += timeout_ms / 1000;
    limite.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if(limite.tv_nsec >= 1000000000L) {
        limite.tv_nsec -= 1000000000L;
        limite.tv_sec++;
    }

    pthread_mutex_lock(&_mtx_missoes);

    while(!_missoes.empty()) {
        if(pthread_cond_timedwait(&_cv_missoes, &_mtx_missoes, &limite) == ETIMEDOUT)
            break;
    }

    bool terminaram = _missoes.empty();

    pthread_mutex_unlock(&_mtx_missoes);

    return terminaram;
}

void Robo_manager::solicitar_encerramento(void) {

    _encerrando = true;
}

bool Robo_manager::encerrando(void) {

    return _encerrando;
}

int Robo_manager::add_robo(Robo* robo) {

    std::lock_guard<std::mutex> lock(m);

    _robo.push_back(robo);

    return _robo.size();
}
        
int Robo_manager::set_robo_to_z_recarga(Robo* robo, Zona_recarga* z_recarga) {

    Node* f_node = z_recarga->get_free_spot();

    Node* i_node = this->get_nearest_node(robo);

    robo->go_to_pos(i_node, f_node);

    return 0;
}

Node* Robo_manager::get_nearest_node(Robo* robo) {

    Node* node = nullptr;

    float min_dist = 3000;

    for(auto n : this->_grafo) {

        float dist = sqrt(pow((n->get_pos_x() - robo->get_pos_x()), 2) + pow((n->get_pos_y() - robo->get_pos_y()), 2));

        if(dist < min_dist) {

            min_dist = dist;
            node = n;
        }
    }

    return node;
}

int Robo_manager::wait(void) {

    return 0;    
}

int Robo_manager::get_new_tarefa(void) {

    m.lock();

    int ret = _num_tarefas;

    _num_tarefas++;

    m.unlock();

    return ret;
}
