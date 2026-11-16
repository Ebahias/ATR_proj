#!/usr/bin/env bash
# Teste de integração do Simulador / Tela de Operação (Python) contra o sistema real:
# broker MQTT + Organizador (que sobe o Gestor e o Coletor) + simulador.
# Executar a partir da raiz do projeto:  make teste-simulador
#
# Requer: python3 com pygame e paho-mqtt (pip install -r simulador/requirements.txt),
#         mosquitto (o broker) e o projeto compilado (make).

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_simulador
mkdir -p "$LOGS" data
rm -f "$LOGS"/*.log "$LOGS"/*.png /dev/shm/atr_frota /dev/mqueue/atr_*

if ! python3 -c "import pygame, paho.mqtt.client" 2>/dev/null; then
    echo "faltam dependências Python: pip install -r simulador/requirements.txt"
    exit 2
fi

PID_BROKER=""
if ! (exec 3<>/dev/tcp/localhost/1883) 2>/dev/null; then
    command -v mosquitto >/dev/null || { echo "broker MQTT não encontrado (instale o mosquitto)"; exit 2; }
    mosquitto -p 1883 > $LOGS/broker.log 2>&1 &
    PID_BROKER=$!
    sleep 1
fi

# ATR_HW=mqtt: os robôs usam o hardware do simulador (Python) em vez da física interna
# --sem-tela: o teste roda o próprio simulador (dois publicariam as entradas
# dos mesmos robôs)
ATR_HW=mqtt ./bin/org_logistica --sem-tela > $LOGS/organizador.log 2>&1 &
PID_ORG=$!
sleep 2

python3 simulador/testes/integracao.py
resultado=$?

kill -INT $PID_ORG 2>/dev/null
sleep 2
kill $PID_ORG 2>/dev/null
[ -n "$PID_BROKER" ] && kill $PID_BROKER 2>/dev/null
wait 2>/dev/null

echo "Logs e capturas de tela em $LOGS/"
exit $resultado
