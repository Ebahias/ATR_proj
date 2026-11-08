#include "gestor_robos/fonte_mqtt.hpp"
#include "comum/mqtt_topicos.hpp"
#include "comum/log.hpp"


FonteMqtt::FonteMqtt() {

    _parar = false;

    pthread_mutex_init(&_mtx, NULL);
    pthread_cond_init(&_cv, NULL);
}

FonteMqtt::~FonteMqtt() {

    _cliente.desconectar();

    pthread_cond_destroy(&_cv);
    pthread_mutex_destroy(&_mtx);
}

bool FonteMqtt::iniciar(void) {

    _cliente.definir_callback([this](const std::string& topico, const std::string& payload) {
        receber(topico, payload);
    });

    if(!_cliente.conectar("gestor"))
        return false;

    if(!_cliente.assinar(MQTT_TOPICO_NOVO_ROBO, MQTT_QOS_NOVO_ROBO))
        return false;

    log_msg("FONTE", "aguardando anúncios de robôs no tópico MQTT %s", MQTT_TOPICO_NOVO_ROBO);

    return true;
}

void FonteMqtt::receber(const std::string&, const std::string& payload) {

    MsgNovoRobo msg;

    if(!parse_msg_novo_robo(payload, msg)) {
        log_msg("FONTE", "mensagem inválida ignorada: \"%s\"", payload.c_str());
        return;
    }

    pthread_mutex_lock(&_mtx);
    _fila.push_back(msg);
    pthread_cond_signal(&_cv);
    pthread_mutex_unlock(&_mtx);
}

int FonteMqtt::aguardar(MsgNovoRobo& msg) {

    pthread_mutex_lock(&_mtx);

    while(_fila.empty() && !_parar)
        pthread_cond_wait(&_cv, &_mtx);

    if(_fila.empty()) {
        pthread_mutex_unlock(&_mtx);
        return 0;
    }

    msg = _fila.front();
    _fila.pop_front();

    pthread_mutex_unlock(&_mtx);

    return 1;
}

void FonteMqtt::interromper(void) {

    pthread_mutex_lock(&_mtx);
    _parar = true;
    pthread_cond_broadcast(&_cv);
    pthread_mutex_unlock(&_mtx);
}
