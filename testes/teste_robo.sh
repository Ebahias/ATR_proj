#!/usr/bin/env bash
# Teste de integração do processo do robô (tarefa 2) com o Gestor de Robôs.
# Executar a partir da raiz do projeto:  make teste-robo

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_robo
FIFO=$LOGS/novo_robo.fifo
mkdir -p "$LOGS"
rm -f "$LOGS"/*.log "$FIFO" /dev/mqueue/atr_* /dev/shm/atr_frota

falhas=0
ok()    { echo "  [OK]    $1"; }
falha() { echo "  [FALHA] $1"; falhas=$((falhas + 1)); }

# Linha do robô <id> na tabela de status da memória compartilhada
linha_robo() { bin/monitor_frota status | awk -v id="$1" 'NR > 1 && $2 == id'; }

# espera <descrição> <comando...>: até 3 s pela condição
espera() {
    local desc=$1; shift
    for _ in $(seq 30); do
        if "$@"; then ok "$desc"; return 0; fi
        sleep 0.1
    done
    falha "$desc"
    return 1
}

status_tem()  { linha_robo "$1" | grep -q -- "$2"; }
evento_tem()  { grep -q -- "$1" $LOGS/eventos.log; }
sem_robo()    { [ -z "$(linha_robo "$1")" ]; }

bin/gestor_robos --fifo "$FIFO" > $LOGS/gestor.log 2>&1 &
PID_GESTOR=$!
sleep 0.3
bin/monitor_frota eventos > $LOGS/eventos.log 2>&1 &
PID_EVENTOS=$!

echo "1. Criação e publicação de status"
bin/novo_robo 1 0 0 --fifo "$FIFO"
bin/novo_robo 2 50 50 --fifo "$FIFO"
espera "robô 1 disponível em (0,0) na memória compartilhada" status_tem 1 "disponível .*(0,0)"
espera "robô 2 disponível em (50,50)" status_tem 2 "disponível .*(50,50)"
[ -e /dev/mqueue/atr_robo_1_cmd ] && [ -e /dev/mqueue/atr_robo_2_cmd ] \
    && ok "filas de comando criadas pelo Gestor" || falha "filas de comando"
evento_tem "robo=1 INICIADO" && ok "evento INICIADO no buffer" || falha "evento INICIADO"
[ "$(ps -L -p "$(linha_robo 1 | awk '{print $3}')" --no-headers | wc -l)" -ge 3 ] \
    && ok "processo do robô com threads de navegação e status" || falha "threads do robô"

echo "2. Comando IR_PARA (Organizador -> fila -> navegação -> hardware -> status)"
bin/comando_robo 1 ir 20 0
espera "robô 1 deslocando" status_tem 1 "deslocando"
espera "robô 1 chegou em (20,0) e ficou disponível" status_tem 1 "disponível .*(20,0) *(20,0)"
evento_tem "robo=1 CHEGOU_DESTINO: chegou em (20, 0)" && ok "evento CHEGOU_DESTINO" || falha "evento CHEGOU_DESTINO"

echo "3. Modo manual / automático"
bin/comando_robo 1 manual --operador
espera "robô 1 em modo manual" status_tem 1 "manual"
bin/comando_robo 1 ir 0 0
sleep 0.5
status_tem 1 "(20,0) *(20,0)" && ok "comando do Organizador ignorado em manual" || falha "Organizador moveu robô em manual"
bin/comando_robo 1 ir 25 0 --operador
espera "comando do operador aceito em manual" status_tem 1 "disponível .*(25,0)"
bin/comando_robo 1 auto --operador
espera "robô 1 de volta ao automático" status_tem 1 "automático"

echo "4. Hardware: falha, bateria, acoplamento"
bin/comando_robo 2 falha 1
bin/comando_robo 2 ir 60 50
espera "falha aparece no status" status_tem 2 "(50,50) *(60,50) .* 1 "
sleep 0.5
status_tem 2 "(50,50) *(60,50)" && ok "robô com falha não se move" || falha "robô com falha se moveu"
bin/comando_robo 2 falha 0
espera "sem falha, robô volta a andar" status_tem 2 "(60,50) *(60,50)"
bin/comando_robo 2 bateria 30
espera "bateria 30% no status" status_tem 2 " 30% "
bin/comando_robo 2 acoplar
espera "carga atracada após acoplar" status_tem 2 "operando carga .* 30%     1 "
bin/comando_robo 2 soltar
espera "carga solta" status_tem 2 "disponível .* 30%     0 "

echo "5. Robô morre (kill -9): Gestor libera slot e fila"
kill -KILL "$(linha_robo 1 | awk '{print $3}')"
espera "slot do robô 1 liberado" sem_robo 1
[ ! -e /dev/mqueue/atr_robo_1_cmd ] && ok "fila do robô 1 removida" || falha "fila do robô 1 ainda existe"
bin/comando_robo 1 parar 2>/dev/null && falha "comando para robô inexistente aceito" \
    || ok "comando para robô inexistente recusado"
status_tem 2 "disponível" && ok "robô 2 não foi afetado" || falha "robô 2 afetado"

echo "6. Encerramento"
kill -TERM $PID_GESTOR
wait $PID_GESTOR
[ $? -eq 0 ] && ok "Gestor saiu com código 0" || falha "código de saída do Gestor"
espera "evento ENCERRADO do robô 2" evento_tem "robo=2 ENCERRADO"
[ -z "$(ls /dev/mqueue | grep atr_robo_)" ] && ok "nenhuma fila de robô sobrando" || falha "filas sobrando"
grep -q "STATUS\] encerrada: .* 0 atrasos" $LOGS/gestor.log \
    && grep -q "NAV\] encerrada: .* 0 atrasos" $LOGS/gestor.log \
    && ok "threads periódicas sem perda de prazo" || falha "houve perda de prazo (ver log)"

kill $PID_EVENTOS 2>/dev/null
wait $PID_EVENTOS 2>/dev/null

echo
echo "Logs em $LOGS/"
if [ $falhas -eq 0 ]; then echo "TODOS OS TESTES PASSARAM"; else echo "$falhas TESTE(S) FALHARAM"; fi
exit $falhas
