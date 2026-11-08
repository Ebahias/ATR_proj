#ifndef FONTE_MQTT_HPP
#define FONTE_MQTT_HPP

#include "gestor_robos/fonte_novos_robos.hpp"
#include "comum/cliente_mqtt.hpp"

#include <deque>
#include <string>

#include <pthread.h>

class FonteMqtt : public FonteNovosRobos {

    protected:

        ClienteMqtt _cliente;

        pthread_mutex_t _mtx;

        pthread_cond_t _cv;

        std::deque<MsgNovoRobo> _fila;

        bool _parar;

        void receber(const std::string& topico, const std::string& payload);

    public:

        FonteMqtt();

        ~FonteMqtt() override;

        bool iniciar(void) override;

        int aguardar(MsgNovoRobo& msg) override;

        void interromper(void) override;
};

#endif
