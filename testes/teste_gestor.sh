#!/usr/bin/env bash
# Teste de integração do Gestor de Robôs (tarefa 1).
# Executar a partir da raiz do projeto:  make teste-gestor

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_gestor
FIFO=$LOGS/novo_robo.fifo
mkdir -p "$LOGS"
rm -f "$LOGS"/*.log "$FIFO" /dev/mqueue/atr_gestor_organizador

falhas=0
ok()    { echo "  [OK]    $1"; }
falha() { echo "  [FALHA] $1"; falhas=$((falhas + 1)); }

# espera_log <arquivo> <padrão> <quantidade>: até 3 s pelo número de linhas
espera_log() {
    for _ in $(seq 30); do
        [ "$(grep -c -- "$2" "$1")" -ge "$3" ] && return 0
        sleep 0.1
    done
    return 1
}

pid_do_robo() {
    grep "robô $1 criado" $LOGS/gestor.log | tail -1 | sed -E 's/.*pid ([0-9]+).*/\1/'
}

bin/escuta_gestor > $LOGS/escuta.log 2>&1 &
PID_ESCUTA=$!
bin/gestor_robos --fifo "$FIFO" > $LOGS/gestor.log 2>&1 &
PID_GESTOR=$!
sleep 0.3

echo "1. Criação de robôs (posição fixa e arbitrária)"
bin/novo_robo 1 --fifo "$FIFO"
bin/novo_robo 2 10 20 --fifo "$FIFO"
bin/novo_robo 3 5 5 --fifo "$FIFO"
espera_log $LOGS/escuta.log "ROBO_NOVO" 3 \
    && ok "Organizador recebeu 3 eventos ROBO_NOVO" || falha "eventos ROBO_NOVO"
grep -q "ROBO_NOVO robo=2 pid=[0-9]* pos=(10, 20)" $LOGS/escuta.log \
    && ok "posição arbitrária repassada" || falha "posição do robô 2"
grep -q "ROBO_NOVO robo=1 pid=[0-9]* pos=(0, 0)" $LOGS/escuta.log \
    && ok "posição fixa padrão" || falha "posição padrão do robô 1"
[ "$(pgrep -P $PID_GESTOR | wc -l)" -eq 3 ] \
    && ok "3 processos filhos do Gestor" || falha "número de processos filhos"

echo "2. Anúncio duplicado e mensagem inválida"
bin/novo_robo 2 1 1 --fifo "$FIFO"
echo "LIXO 1 2" > "$FIFO"
espera_log $LOGS/gestor.log "robô 2 já está ativo" 1 \
    && ok "duplicata recusada" || falha "duplicata"
espera_log $LOGS/gestor.log "mensagem inválida ignorada" 1 \
    && ok "mensagem inválida ignorada" || falha "mensagem inválida"

echo "3. Robô morre inesperadamente"
kill -KILL "$(pid_do_robo 2)"
espera_log $LOGS/escuta.log "ROBO_REMOVIDO robo=2 .* sinal=9" 1 \
    && ok "Organizador avisado da remoção do robô 2" || falha "remoção do robô 2"
bin/novo_robo 2 7 7 --fifo "$FIFO"
espera_log $LOGS/escuta.log "ROBO_NOVO robo=2 pid=[0-9]* pos=(7, 7)" 1 \
    && ok "robô 2 pode voltar após remoção" || falha "recriar robô 2"

echo "4. Rajada de 20 anúncios simultâneos"
for i in $(seq 100 119); do bin/novo_robo $i $i $i --fifo "$FIFO" & done
wait $(jobs -p | grep -v -e $PID_GESTOR -e $PID_ESCUTA) 2>/dev/null
espera_log $LOGS/escuta.log "ROBO_NOVO" 24 \
    && ok "20 robôs criados sem perda de mensagens" || falha "rajada"
[ "$(pgrep -P $PID_GESTOR | wc -l)" -eq 23 ] \
    && ok "23 processos filhos ativos" || falha "contagem após rajada"

# Ordem: para o mesmo robô, ROBO_NOVO sempre antes de ROBO_REMOVIDO
awk '/ROBO_NOVO/ {match($0,/robo=[0-9]+/); n[substr($0,RSTART,RLENGTH)]=1}
     /ROBO_REMOVIDO/ {match($0,/robo=[0-9]+/); if(!n[substr($0,RSTART,RLENGTH)]) bad=1}
     END {exit bad}' $LOGS/escuta.log \
    && ok "ordem dos eventos preservada" || falha "ordem dos eventos"

echo "5. Encerramento"
FILHOS=$(pgrep -P $PID_GESTOR)
kill -TERM $PID_GESTOR
wait $PID_GESTOR
[ $? -eq 0 ] && ok "Gestor saiu com código 0" || falha "código de saída do Gestor"
vivos=0
for p in $FILHOS; do kill -0 "$p" 2>/dev/null && vivos=$((vivos + 1)); done
[ $vivos -eq 0 ] && ok "nenhum processo de robô órfão" || falha "$vivos robôs ainda vivos"
espera_log $LOGS/escuta.log "ROBO_REMOVIDO" 24 \
    && ok "Organizador recebeu a remoção de todos os robôs" || falha "remoções no encerramento"

kill $PID_ESCUTA 2>/dev/null
wait $PID_ESCUTA 2>/dev/null

echo
echo "Logs em $LOGS/"
if [ $falhas -eq 0 ]; then echo "TODOS OS TESTES PASSARAM"; else echo "$falhas TESTE(S) FALHARAM"; fi
exit $falhas
