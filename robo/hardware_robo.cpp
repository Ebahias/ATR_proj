#include "robo/hardware_robo.hpp"
#include "robo/config_robo.hpp"
#include "comum/periodico.hpp"

#include <cmath>


HardwareSimulado::HardwareSimulado(int x, int y) {

    _rodando = false;
    _x = x;
    _y = y;
    _sp_x = x;
    _sp_y = y;
    _bateria = 100.0;
    _falha = false;
    _acoplar = false;
    _atracada = false;
    _t_acoplando = 0.0;

    pthread_mutex_init(&_mtx, NULL);
}

HardwareSimulado::~HardwareSimulado() {

    parar();

    pthread_mutex_destroy(&_mtx);
}

bool HardwareSimulado::iniciar(void) {

    _rodando = true;

    if(pthread_create(&_thread, NULL, HardwareSimulado::thread_fisica, this) != 0) {
        _rodando = false;
        return false;
    }

    return true;
}

void HardwareSimulado::parar(void) {

    pthread_mutex_lock(&_mtx);
    bool estava_rodando = _rodando;
    _rodando = false;
    pthread_mutex_unlock(&_mtx);

    if(estava_rodando)
        pthread_join(_thread, NULL);
}

void* HardwareSimulado::thread_fisica(void* arg) {

    ((HardwareSimulado*)arg)->loop_fisica();

    return NULL;
}

void HardwareSimulado::loop_fisica(void) {

    Periodo periodo(PERIODO_HARDWARE_MS);
    const double dt = PERIODO_HARDWARE_MS / 1000.0;

    while(true) {

        pthread_mutex_lock(&_mtx);

        if(!_rodando) {
            pthread_mutex_unlock(&_mtx);
            break;
        }

        passo(dt);

        pthread_mutex_unlock(&_mtx);

        periodo.esperar();
    }
}

// Chamado com _mtx travado
void HardwareSimulado::passo(double dt) {

    double dx = _sp_x - _x;
    double dy = _sp_y - _y;
    double dist = std::sqrt(dx * dx + dy * dy);

    bool pode_mover = !_falha && _bateria > 0.0;
    bool moveu = false;

    if(pode_mover && dist > 0.0) {

        double passo = VELOCIDADE_ROBO * dt;

        if(dist <= passo) {
            _x = _sp_x;
            _y = _sp_y;
        } else {
            _x += dx / dist * passo;
            _y += dy / dist * passo;
        }

        moveu = true;
    }

    _bateria -= (moveu ? DRENO_BATERIA_MOVENDO : DRENO_BATERIA_PARADO) * dt;

    if(_bateria < 0.0)
        _bateria = 0.0;

    if(_acoplar && !_atracada && !_falha) {
        _t_acoplando += dt;
        if(_t_acoplando >= TEMPO_ACOPLAR_S)
            _atracada = true;
    } else if(!_acoplar) {
        _atracada = false;
        _t_acoplando = 0.0;
    }
}

LeituraHardware HardwareSimulado::ler(void) {

    LeituraHardware l;

    pthread_mutex_lock(&_mtx);

    l.pos_x = (int)std::lround(_x);
    l.pos_y = (int)std::lround(_y);
    l.carga_atracada = _atracada;
    l.bateria = (int)std::ceil(_bateria);
    l.falha = _falha;

    pthread_mutex_unlock(&_mtx);

    return l;
}

void HardwareSimulado::escrever_setpoint(int x, int y) {

    pthread_mutex_lock(&_mtx);
    _sp_x = x;
    _sp_y = y;
    pthread_mutex_unlock(&_mtx);
}

void HardwareSimulado::acoplar_e_erguer(bool ativo) {

    pthread_mutex_lock(&_mtx);
    _acoplar = ativo;
    pthread_mutex_unlock(&_mtx);
}

bool HardwareSimulado::simular_falha(bool falha) {

    pthread_mutex_lock(&_mtx);
    _falha = falha;
    pthread_mutex_unlock(&_mtx);

    return true;
}

bool HardwareSimulado::simular_bateria(int percentual) {

    if(percentual < 0 || percentual > 100)
        return false;

    pthread_mutex_lock(&_mtx);
    _bateria = percentual;
    pthread_mutex_unlock(&_mtx);

    return true;
}
