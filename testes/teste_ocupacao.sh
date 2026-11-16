#!/usr/bin/env bash
# Mede o aproveitamento da frota (missões em paralelo, robôs ociosos com caixa esperando).
# Uso:  ./testes/teste_ocupacao.sh [robôs] [segundos]     (padrão: 5 robôs, 150 s)
# Requer: mosquitto, pygame e paho-mqtt (ver simulador/README.md) e o projeto compilado.

cd "$(dirname "$0")/.." || exit 1

ROBOS=${1:-5}
TEMPO=${2:-150}
LOGS=run/teste_ocupacao
mkdir -p "$LOGS" data
rm -f "$LOGS"/*.log /dev/shm/atr_frota /dev/mqueue/atr_*

PID_BROKER=""
if ! (exec 3<>/dev/tcp/localhost/1883) 2>/dev/null; then
    mosquitto -p 1883 > $LOGS/broker.log 2>&1 &
    PID_BROKER=$!
    sleep 1
fi

# --sem-tela: o teste roda o próprio simulador (dois publicariam as entradas
# dos mesmos robôs)
ATR_HW=mqtt ./bin/org_logistica --sem-tela > $LOGS/organizador.log 2>&1 &
PID_ORG=$!
sleep 2

python3 simulador/testes/ocupacao.py --robos "$ROBOS" --tempo "$TEMPO" | tee $LOGS/resultado.txt
resultado=${PIPESTATUS[0]}

kill -INT $PID_ORG 2>/dev/null
sleep 2
kill $PID_ORG 2>/dev/null
[ -n "$PID_BROKER" ] && kill $PID_BROKER 2>/dev/null
wait 2>/dev/null
exit $resultado
