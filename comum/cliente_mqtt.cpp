#include "comum/cliente_mqtt.hpp"
#include "comum/mqtt_topicos.hpp"
#include "comum/log.hpp"

#include <cstdlib>

#include <mosquitto.h>


static pthread_once_t lib_once = PTHREAD_ONCE_INIT;

void ClienteMqtt::iniciar_lib(void) {

    mosquitto_lib_init();
}

ClienteMqtt::ClienteMqtt() {

    pthread_once(&lib_once, ClienteMqtt::iniciar_lib);

    _mosq = NULL;
    _conectado = false;
    _desconexao_pedida = false;
    _loop_ativo = false;
    _tem_will = false;
    _will_qos = 0;
    _will_retain = false;
    _porta = MQTT_PORTA_PADRAO;

    pthread_mutex_init(&_mtx, NULL);
}

ClienteMqtt::~ClienteMqtt() {

    desconectar();

    if(_mosq != NULL) {
        mosquitto_destroy(_mosq);
        _mosq = NULL;
    }

    pthread_mutex_destroy(&_mtx);
}

void ClienteMqtt::definir_callback(CallbackMensagem cb) {

    pthread_mutex_lock(&_mtx);
    _callback = cb;
    pthread_mutex_unlock(&_mtx);
}

bool ClienteMqtt::definir_will(const std::string& topico, const std::string& payload, int qos, bool retain) {

    if(topico.empty() || qos < 0 || qos > 2)
        return false;

    pthread_mutex_lock(&_mtx);

    if(_mosq != NULL) {
        pthread_mutex_unlock(&_mtx);
        log_msg("MQTT", "definir_will depois de conectar: ignorado");
        return false;
    }

    _tem_will = true;
    _will_topico = topico;
    _will_payload = payload;
    _will_qos = qos;
    _will_retain = retain;

    pthread_mutex_unlock(&_mtx);

    return true;
}

bool ClienteMqtt::conectar(const std::string& client_id) {

    if(_mosq != NULL) {
        log_msg("MQTT", "conectar chamado com o cliente já criado");
        return false;
    }

    const char* env_host = getenv(MQTT_ENV_HOST);
    const char* env_porta = getenv(MQTT_ENV_PORTA);

    _host = (env_host != NULL && env_host[0] != '\0') ? env_host : MQTT_HOST_PADRAO;
    _porta = MQTT_PORTA_PADRAO;

    if(env_porta != NULL && env_porta[0] != '\0') {
        char* fim = NULL;
        long p = strtol(env_porta, &fim, 10);
        if(*fim == '\0' && p > 0 && p <= 65535)
            _porta = (int)p;
        else
            log_msg("MQTT", "%s inválida (%s), usando %d", MQTT_ENV_PORTA, env_porta, MQTT_PORTA_PADRAO);
    }

    struct mosquitto* m = mosquitto_new(client_id.empty() ? NULL : client_id.c_str(), true, this);
    if(m == NULL) {
        log_msg("MQTT", "mosquitto_new falhou para '%s'", client_id.c_str());
        return false;
    }

    mosquitto_connect_callback_set(m, ClienteMqtt::on_connect);
    mosquitto_disconnect_callback_set(m, ClienteMqtt::on_disconnect);
    mosquitto_message_callback_set(m, ClienteMqtt::on_message);

    pthread_mutex_lock(&_mtx);
    bool tem_will = _tem_will;
    std::string will_topico = _will_topico;
    std::string will_payload = _will_payload;
    int will_qos = _will_qos;
    bool will_retain = _will_retain;
    pthread_mutex_unlock(&_mtx);

    if(tem_will) {
        int rc = mosquitto_will_set(m, will_topico.c_str(), (int)will_payload.size(),
                                    will_payload.data(), will_qos, will_retain);
        if(rc != MOSQ_ERR_SUCCESS) {
            log_msg("MQTT", "mosquitto_will_set falhou: %s", mosquitto_strerror(rc));
            mosquitto_destroy(m);
            return false;
        }
    }

    mosquitto_reconnect_delay_set(m, 1, 10, true);

    _desconexao_pedida = false;

    int rc = mosquitto_connect(m, _host.c_str(), _porta, MQTT_KEEPALIVE_S);
    if(rc != MOSQ_ERR_SUCCESS) {
        log_msg("MQTT", "falha ao conectar em %s:%d: %s", _host.c_str(), _porta, mosquitto_strerror(rc));
        mosquitto_destroy(m);
        return false;
    }

    pthread_mutex_lock(&_mtx);
    _mosq = m;
    pthread_mutex_unlock(&_mtx);

    rc = mosquitto_loop_start(m);
    if(rc != MOSQ_ERR_SUCCESS) {
        log_msg("MQTT", "mosquitto_loop_start falhou: %s", mosquitto_strerror(rc));
        mosquitto_disconnect(m);
        pthread_mutex_lock(&_mtx);
        _mosq = NULL;
        pthread_mutex_unlock(&_mtx);
        mosquitto_destroy(m);
        return false;
    }

    _loop_ativo = true;

    return true;
}

bool ClienteMqtt::assinar(const std::string& topico, int qos) {

    if(topico.empty() || qos < 0 || qos > 2)
        return false;

    pthread_mutex_lock(&_mtx);
    _assinaturas.push_back(std::make_pair(topico, qos));
    struct mosquitto* m = _mosq;
    pthread_mutex_unlock(&_mtx);

    if(m == NULL || !_conectado)
        return true;

    int rc = mosquitto_subscribe(m, NULL, topico.c_str(), qos);
    if(rc != MOSQ_ERR_SUCCESS) {
        log_msg("MQTT", "falha ao assinar '%s': %s", topico.c_str(), mosquitto_strerror(rc));
        return false;
    }

    return true;
}

bool ClienteMqtt::publicar(const std::string& topico, const std::string& payload, int qos, bool retain) {

    if(!_conectado)
        return false;

    struct mosquitto* m = _mosq;

    if(m == NULL)
        return false;

    int rc = mosquitto_publish(m, NULL, topico.c_str(), (int)payload.size(), payload.data(), qos, retain);
    if(rc != MOSQ_ERR_SUCCESS) {
        log_msg("MQTT", "falha ao publicar em '%s': %s", topico.c_str(), mosquitto_strerror(rc));
        return false;
    }

    return true;
}

void ClienteMqtt::desconectar(void) {

    if(_mosq == NULL || !_loop_ativo)
        return;

    _desconexao_pedida = true;

    mosquitto_disconnect(_mosq);
    mosquitto_loop_stop(_mosq, false);

    _loop_ativo = false;
    _conectado = false;
}

bool ClienteMqtt::conectado(void) const {

    return _conectado;
}

void ClienteMqtt::on_connect(struct mosquitto*, void* obj, int rc) {

    ((ClienteMqtt*)obj)->tratar_conexao(rc);
}

void ClienteMqtt::on_disconnect(struct mosquitto*, void* obj, int rc) {

    ((ClienteMqtt*)obj)->tratar_desconexao(rc);
}

void ClienteMqtt::on_message(struct mosquitto*, void* obj, const struct mosquitto_message* msg) {

    ((ClienteMqtt*)obj)->tratar_mensagem(msg);
}

void ClienteMqtt::tratar_conexao(int rc) {

    if(rc != 0) {
        log_msg("MQTT", "conexão recusada por %s:%d: %s", _host.c_str(), _porta, mosquitto_connack_string(rc));
        return;
    }

    _conectado = true;

    pthread_mutex_lock(&_mtx);
    std::vector<std::pair<std::string, int>> assinaturas = _assinaturas;
    struct mosquitto* m = _mosq;
    pthread_mutex_unlock(&_mtx);

    log_msg("MQTT", "conectado a %s:%d (%zu assinaturas)", _host.c_str(), _porta, assinaturas.size());

    if(m == NULL)
        return;

    for(size_t i=0;i<assinaturas.size();i++) {
        int r = mosquitto_subscribe(m, NULL, assinaturas[i].first.c_str(), assinaturas[i].second);
        if(r != MOSQ_ERR_SUCCESS)
            log_msg("MQTT", "falha ao reassinar '%s': %s", assinaturas[i].first.c_str(), mosquitto_strerror(r));
    }
}

void ClienteMqtt::tratar_desconexao(int rc) {

    _conectado = false;

    if(!_desconexao_pedida)
        log_msg("MQTT", "conexão com %s:%d perdida (%s), reconectando", _host.c_str(), _porta, mosquitto_strerror(rc));
}

void ClienteMqtt::tratar_mensagem(const struct mosquitto_message* msg) {

    pthread_mutex_lock(&_mtx);
    CallbackMensagem cb = _callback;
    pthread_mutex_unlock(&_mtx);

    if(!cb)
        return;

    std::string topico(msg->topic != NULL ? msg->topic : "");
    std::string payload((const char*)msg->payload, msg->payloadlen);

    cb(topico, payload);
}
