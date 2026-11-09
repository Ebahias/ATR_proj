#include "robo/hardware_robo.hpp"
#include "robo/config_robo.hpp"
#include "comum/periodico.hpp"
#include "comum/mapa.hpp"
#include "comum/mqtt_topicos.hpp"
#include "comum/log.hpp"

#include <cmath>
#include <cstdio>
#include <unistd.h>


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

    // Parado numa estação de recarga: carrega (o simulador conhece o mapa)
    const AreaMapa* area = mapa_area_do_ponto((int)std::lround(_x), (int)std::lround(_y));

    if(!moveu && !_falha && area && area->tipo == AREA_RECARGA)
        _bateria += TAXA_RECARGA * dt;

    if(_bateria < 0.0)
        _bateria = 0.0;

    if(_bateria > 100.0)
        _bateria = 100.0;

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


HardwareMqtt::HardwareMqtt(int id, int x, int y) {

    _id = id;

    _leitura.pos_x = x;
    _leitura.pos_y = y;
    _leitura.carga_atracada = false;
    _leitura.bateria = 100;
    _leitura.falha = false;

    _recebeu_entrada = false;
    clock_gettime(CLOCK_MONOTONIC, &_t_ultima_entrada);

    _sp_x = x;
    _sp_y = y;
    _acoplar = false;

    _topico_entradas = mqtt_topico_entradas(id);

    pthread_mutex_init(&_mtx, NULL);
}

HardwareMqtt::~HardwareMqtt() {

    parar();

    pthread_mutex_destroy(&_mtx);
}

bool HardwareMqtt::iniciar(void) {

    std::string topico_vivo = mqtt_topico_vivo(_id);

    _cliente.definir_will(topico_vivo, MQTT_VIVO_OFFLINE, MQTT_QOS_VIVO, MQTT_RETAIN_VIVO);

    _cliente.definir_callback([this](const std::string& topico, const std::string& payload) {
        receber(topico, payload);
    });

    std::string client_id = "robo-" + std::to_string(_id) + "-" + std::to_string(getpid());

    if(!_cliente.conectar(client_id))
        return false;

    if(!_cliente.assinar(_topico_entradas, MQTT_QOS_ENTRADAS))
        return false;

    for(int i=0;i<50 && !_cliente.conectado();i++)
        usleep(10000);

    if(!_cliente.conectado()) {
        log_msg("MQTT", "robô %d: sem conexão com o broker após 500 ms", _id);
        return false;
    }

    _cliente.publicar(topico_vivo, MQTT_VIVO_ONLINE, MQTT_QOS_VIVO, MQTT_RETAIN_VIVO);

    pthread_mutex_lock(&_mtx);
    int sp_x = _sp_x;
    int sp_y = _sp_y;
    bool acoplar = _acoplar;
    pthread_mutex_unlock(&_mtx);

    publicar_saidas(sp_x, sp_y, acoplar);

    return true;
}

void HardwareMqtt::parar(void) {

    if(_cliente.conectado())
        _cliente.publicar(mqtt_topico_vivo(_id), MQTT_VIVO_OFFLINE, MQTT_QOS_VIVO, MQTT_RETAIN_VIVO);

    _cliente.desconectar();
}

void HardwareMqtt::receber(const std::string& topico, const std::string& payload) {

    if(topico != _topico_entradas)
        return;

    int x, y, atracada, bateria, falha;

    if(sscanf(payload.c_str(), MQTT_FMT_ENTRADAS, &x, &y, &atracada, &bateria, &falha) != MQTT_N_CAMPOS_ENTRADAS)
        return;

    pthread_mutex_lock(&_mtx);

    _leitura.pos_x = x;
    _leitura.pos_y = y;
    _leitura.carga_atracada = atracada != 0;
    _leitura.bateria = bateria;
    _leitura.falha = falha != 0;

    _recebeu_entrada = true;
    clock_gettime(CLOCK_MONOTONIC, &_t_ultima_entrada);

    pthread_mutex_unlock(&_mtx);
}

LeituraHardware HardwareMqtt::ler(void) {

    struct timespec agora;
    clock_gettime(CLOCK_MONOTONIC, &agora);

    pthread_mutex_lock(&_mtx);

    LeituraHardware l = _leitura;

    long desde_ms = (agora.tv_sec - _t_ultima_entrada.tv_sec) * 1000L
                  + (agora.tv_nsec - _t_ultima_entrada.tv_nsec) / 1000000L;

    if(_recebeu_entrada && desde_ms > MQTT_TIMEOUT_ENTRADAS_MS)
        l.falha = true;

    pthread_mutex_unlock(&_mtx);

    return l;
}

void HardwareMqtt::escrever_setpoint(int x, int y) {

    pthread_mutex_lock(&_mtx);

    bool mudou = (x != _sp_x || y != _sp_y);
    _sp_x = x;
    _sp_y = y;
    bool acoplar = _acoplar;

    pthread_mutex_unlock(&_mtx);

    if(mudou)
        publicar_saidas(x, y, acoplar);
}

void HardwareMqtt::acoplar_e_erguer(bool ativo) {

    pthread_mutex_lock(&_mtx);

    bool mudou = (ativo != _acoplar);
    _acoplar = ativo;
    int sp_x = _sp_x;
    int sp_y = _sp_y;

    pthread_mutex_unlock(&_mtx);

    if(mudou)
        publicar_saidas(sp_x, sp_y, ativo);
}

void HardwareMqtt::publicar_saidas(int sp_x, int sp_y, bool acoplar) {

    char payload[64];
    snprintf(payload, sizeof(payload), MQTT_FMT_SAIDAS, sp_x, sp_y, acoplar ? 1 : 0);

    _cliente.publicar(mqtt_topico_saidas(_id), payload, MQTT_QOS_SAIDAS, MQTT_RETAIN_SAIDAS);
}
