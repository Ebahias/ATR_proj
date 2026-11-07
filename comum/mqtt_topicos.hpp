#ifndef COMUM_MQTT_TOPICOS_HPP
#define COMUM_MQTT_TOPICOS_HPP

#include <string>

#define MQTT_HOST_PADRAO                "localhost"
#define MQTT_PORTA_PADRAO               1883
#define MQTT_ENV_HOST                   "ATR_MQTT_HOST"
#define MQTT_ENV_PORTA                  "ATR_MQTT_PORT"
#define MQTT_KEEPALIVE_S                10

#define MQTT_TOPICO_NOVO_ROBO           "atr/robos/novo"
#define MQTT_TOPICO_FROTA_STATUS        "atr/frota/status"
#define MQTT_TOPICO_EVENTOS             "atr/eventos"
#define MQTT_TOPICO_CARGAS              "atr/cargas"
#define MQTT_TOPICO_OPERACAO_CMD        "atr/operacao/cmd"

#define MQTT_FILTRO_ENTRADAS            "atr/robo/+/entradas"
#define MQTT_FILTRO_SAIDAS              "atr/robo/+/saidas"
#define MQTT_FILTRO_VIVO                "atr/robo/+/vivo"

#define MQTT_QOS_NOVO_ROBO              1
#define MQTT_QOS_ENTRADAS               0
#define MQTT_QOS_SAIDAS                 1
#define MQTT_QOS_VIVO                   1
#define MQTT_QOS_FROTA_STATUS           0
#define MQTT_QOS_EVENTOS                1
#define MQTT_QOS_CARGAS                 1
#define MQTT_QOS_OPERACAO_CMD           1

#define MQTT_RETAIN_NOVO_ROBO           false
#define MQTT_RETAIN_ENTRADAS            false
#define MQTT_RETAIN_SAIDAS              false
#define MQTT_RETAIN_VIVO                true
#define MQTT_RETAIN_FROTA_STATUS        true
#define MQTT_RETAIN_EVENTOS             false
#define MQTT_RETAIN_CARGAS              true
#define MQTT_RETAIN_OPERACAO_CMD        false

#define MQTT_PERIODO_ENTRADAS_MS        50
#define MQTT_PERIODO_FROTA_STATUS_MS    200
#define MQTT_TIMEOUT_ENTRADAS_MS        1000

#define MQTT_FMT_ENTRADAS               "%d %d %d %d %d"
#define MQTT_N_CAMPOS_ENTRADAS          5
#define MQTT_FMT_SAIDAS                 "%d %d %d"
#define MQTT_N_CAMPOS_SAIDAS            3

#define MQTT_VIVO_ONLINE                "online"
#define MQTT_VIVO_OFFLINE               "offline"

#define MQTT_CMD_MODO                   "MODO"
#define MQTT_CMD_IR                     "IR"
#define MQTT_CMD_PARAR                  "PARAR"
#define MQTT_CMD_CAIXA                  "CAIXA"
#define MQTT_MODO_MANUAL                "manual"
#define MQTT_MODO_AUTO                  "auto"

inline std::string mqtt_topico_robo(int id, const char* sufixo) {
    return "atr/robo/" + std::to_string(id) + "/" + sufixo;
}

inline std::string mqtt_topico_entradas(int id) {
    return mqtt_topico_robo(id, "entradas");
}

inline std::string mqtt_topico_saidas(int id) {
    return mqtt_topico_robo(id, "saidas");
}

inline std::string mqtt_topico_vivo(int id) {
    return mqtt_topico_robo(id, "vivo");
}

#endif
