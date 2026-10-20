#!/usr/bin/env bash
# Teste de integração do Coletor de Dados (tarefa 6): histórico por robô,
# rastreabilidade das cargas, persistência em disco e canal para o Organizador.
# Executar a partir da raiz do projeto:  make teste-coletor

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_coletor
FIFO=$LOGS/novo_robo.fifo
DADOS=$LOGS/dados
mkdir -p "$LOGS"
rm -rf "$LOGS"/*.log "$FIFO" "$DADOS" /dev/mqueue/atr_* /dev/shm/atr_frota

falhas=0
ok()    { echo "  [OK]    $1"; }
falha() { echo "  [FALHA] $1"; falhas=$((falhas + 1)); }

# campo <id> <nome>: coluna da tabela de status (contando do fim da linha).
# Nomes: pos carga fase
campo() {
    bin/monitor_frota status | awk -v id="$1" -v c="$2" 'NR > 1 && $2 == id {
        m["fase"] = $(NF-3); m["carga"] = $(NF-7); m["pos"] = $(NF-10); print m[c] }'
}

espera() {
    local seg=$1 desc=$2; shift 2
    for _ in $(seq $((seg * 10))); do
        if "$@"; then ok "$desc"; return 0; fi
        sleep 0.1
    done
    falha "$desc"
    return 1
}

campo_eh()    { [ "$(campo "$1" "$2")" = "$3" ]; }
hist_tem()    { grep -q -- "$2" "$DADOS/historico_robo_$1.log" 2>/dev/null; }
cargas_tem()  { grep -q -- "$1" "$DADOS/cargas.csv" 2>/dev/null; }
escuta_tem()  { grep -q -- "$1" $LOGS/escuta.log; }
pid_robo()    { bin/monitor_frota status | awk -v id="$1" '$2 == id {print $3}'; }

# O Coletor é o único consumidor do buffer: não usar "monitor_frota eventos" aqui.
bin/escuta_coletor > $LOGS/escuta.log 2>&1 &
PID_ESCUTA=$!
bin/gestor_robos --fifo "$FIFO" > $LOGS/gestor.log 2>&1 &
PID_GESTOR=$!
sleep 0.3
bin/coletor --dir "$DADOS" > $LOGS/coletor.log 2>&1 &
PID_COLETOR=$!
sleep 0.3

echo "1. Histórico por robô com timestamp"
bin/novo_robo 1 20 15 --fifo "$FIFO"
bin/novo_robo 2 50 105 --fifo "$FIFO"
espera 3 "histórico do robô 1 criado com evento INICIADO" hist_tem 1 "INICIADO"
espera 3 "histórico do robô 2 criado" hist_tem 2 "INICIADO"
bin/comando_robo 1 ir 40 15
espera 3 "evento CHEGOU_DESTINO no histórico" hist_tem 1 "CHEGOU_DESTINO: chegou em (40, 15)"
[ "$(grep -cvE '^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\] [A-Z_]+: ' "$DADOS/historico_robo_1.log")" -eq 0 ] \
    && ok "todas as linhas têm data, hora e ms" || falha "linha sem timestamp"
sort -c "$DADOS/historico_robo_1.log" 2>/dev/null \
    && ok "eventos em ordem cronológica" || falha "histórico fora de ordem"
! grep -q "robo=2\|robô 2" "$DADOS/historico_robo_1.log" \
    && ok "histórico de um robô não mistura outro" || falha "histórico misturado"

echo "2. Defeito, bateria e recarga no histórico"
bin/comando_robo 2 falha 1
espera 3 "defeito presente registrado" hist_tem 2 "DEFEITO: defeito presente"
bin/comando_robo 2 falha 0
espera 3 "defeito removido registrado" hist_tem 2 "DEFEITO: defeito removido"
bin/comando_robo 2 bateria 15
espera 3 "recarga registrada" hist_tem 2 "RECARGA: bateria 15%: indo para Recarga"
espera 8 "recarga concluída registrada" hist_tem 2 "RECARGA: recarga concluída"

echo "3. Rastreabilidade: Baia 3 -> Zona C com robôs 8 e 9"
bin/novo_robo 8 100 90 --fifo "$FIFO"
bin/novo_robo 9 100 90 --fifo "$FIFO"
espera 3 "robôs 8 e 9 prontos" hist_tem 9 "INICIADO"
sleep 0.3
bin/comando_robo 8 tarefa 42 12 75 182 75 --equipe 8,9
bin/comando_robo 9 tarefa 42 18 75 188 75 --equipe 8,9
espera 15 "robôs na coleta" campo_eh 9 fase "aguardando_equipe"
espera 15 "robô 8 na coleta" campo_eh 8 fase "aguardando_equipe"
bin/comando_robo 8 acoplar
bin/comando_robo 9 acoplar
espera 3 "equipe transportando" campo_eh 9 fase "transportando"
! cargas_tem ";42;" && ok "carga ainda em transporte não aparece como entregue" || falha "carga registrada antes da entrega"
espera 20 "robô 8 concluiu a entrega" campo_eh 8 fase "concluida"
espera 5 "carga 42 registrada em cargas.csv" cargas_tem ";42;"
cargas_tem ";42;Baia 3;Zona C;8+9;.*;entregue$" \
    && ok "baia, destino e robôs corretos (Baia 3 -> Zona C, robôs 8+9)" || falha "conteúdo da linha da carga 42"
[ "$(grep -c ';42;' "$DADOS/cargas.csv")" -eq 1 ] \
    && ok "uma única linha por carga (não uma por robô)" || falha "linhas duplicadas da carga"
hist_tem 8 "CARGA: pegou carga na Baia 3 (tarefa 42)" && hist_tem 9 "CARGA: entregou carga na Zona C (tarefa 42)" \
    && ok "eventos 'pegou' e 'entregou' no histórico dos robôs" || falha "eventos de carga no histórico"
[ "$(head -1 "$DADOS/cargas.csv")" = "data_hora_entrega;tarefa_id;baia;destino;robos;data_hora_coleta;situacao" ] \
    && ok "cabeçalho do cargas.csv" || falha "cabeçalho do cargas.csv"

echo "4. Disponibilização ao Organizador"
kill $PID_ESCUTA 2>/dev/null; wait $PID_ESCUTA 2>/dev/null
# A fila só guarda 10 mensagens: a escuta lê o que sobrou e o que chega agora
bin/escuta_coletor > $LOGS/escuta.log 2>&1 &
PID_ESCUTA=$!
sleep 0.3
bin/novo_robo 3 20 45 --fifo "$FIFO"
bin/comando_robo 3 ir 30 45
espera 3 "evento do robô 3 chegou ao Organizador" escuta_tem "EVENTO robo=3 CHEGOU_DESTINO"
bin/novo_robo 4 15 15 --fifo "$FIFO"
bin/novo_robo 5 15 15 --fifo "$FIFO"
sleep 0.5
bin/comando_robo 4 tarefa 7 15 15 185 15 --equipe 4,5
bin/comando_robo 5 tarefa 7 15 15 185 15 --equipe 4,5
espera 15 "robô 4 na coleta" campo_eh 4 fase "aguardando_equipe"
espera 15 "robô 5 na coleta" campo_eh 5 fase "aguardando_equipe"
bin/comando_robo 4 acoplar; bin/comando_robo 5 acoplar
espera 20 "registro de carga chegou ao Organizador" escuta_tem "CARGA tarefa=7 baia=Baia 1 destino=Zona A robos=4+5 completa=1"

echo "5. Persistência: dados sobrevivem a kill -9 e o histórico continua depois"
linhas_antes=$(wc -l < "$DADOS/historico_robo_1.log")
kill -KILL $PID_COLETOR; wait $PID_COLETOR 2>/dev/null
[ "$(wc -l < "$DADOS/historico_robo_1.log")" -eq "$linhas_antes" ] && [ "$linhas_antes" -gt 0 ] \
    && ok "histórico em disco após kill -9 ($linhas_antes linhas)" || falha "dados perdidos no kill -9"
cargas_tem ";42;Baia 3;Zona C;" && ok "rastreabilidade em disco após kill -9" || falha "cargas.csv perdido"
bin/coletor --dir "$DADOS" > $LOGS/coletor2.log 2>&1 &
PID_COLETOR=$!
sleep 0.3
bin/comando_robo 1 ir 20 15
espera 3 "novo Coletor acrescenta ao histórico existente" hist_tem 1 "CHEGOU_DESTINO: chegou em (20, 15)"
[ "$(grep -c '^data_hora_entrega' "$DADOS/cargas.csv")" -eq 1 ] \
    && ok "cabeçalho do cargas.csv não duplicado" || falha "cabeçalho duplicado"
[ "$(grep -c 'INICIADO' "$DADOS/historico_robo_1.log")" -eq 1 ] \
    && ok "histórico anterior preservado (não sobrescrito)" || falha "histórico sobrescrito"

echo "6. Rajada: 20 robôs ao mesmo tempo sem perder eventos"
antes=$(ls "$DADOS"/historico_robo_*.log | wc -l)
for i in $(seq 100 119); do bin/novo_robo $i $((i - 90)) 20 --fifo "$FIFO" & done
wait $(jobs -p | grep -v -e $PID_GESTOR -e $PID_COLETOR -e $PID_ESCUTA) 2>/dev/null
# conta quantos dos 20 históricos têm o padrão
conta() { local n=0; for i in $(seq 100 119); do hist_tem $i "$1" && n=$((n + 1)); done; echo $n; }
tem_20() { [ "$(conta "$1")" -eq 20 ]; }
espera 5 "20 históricos criados, um por robô (INICIADO)" tem_20 "INICIADO"
for i in $(seq 100 119); do bin/comando_robo $i ir $((i - 80)) 20; done
espera 20 "evento CHEGOU_DESTINO dos 20 robôs registrado" tem_20 "CHEGOU_DESTINO"
[ "$(bin/monitor_frota status | grep -c 'eventos descartados: 0')" -eq 1 ] \
    && ok "nenhum evento descartado no buffer compartilhado" || falha "eventos descartados no buffer"

echo "7. Carga incompleta: robô morre durante o transporte"
bin/novo_robo 12 100 90 --fifo "$FIFO"
bin/novo_robo 13 100 90 --fifo "$FIFO"
espera 3 "robôs 12 e 13 prontos" hist_tem 13 "INICIADO"
sleep 0.3
bin/comando_robo 12 tarefa 55 12 105 182 105 --equipe 12,13
bin/comando_robo 13 tarefa 55 18 105 188 105 --equipe 12,13
espera 15 "robô 12 na coleta" campo_eh 12 fase "aguardando_equipe"
espera 15 "robô 13 na coleta" campo_eh 13 fase "aguardando_equipe"
bin/comando_robo 12 acoplar; bin/comando_robo 13 acoplar
espera 3 "equipe transportando" campo_eh 13 fase "transportando"
espera 3 "robô 12 pegou a carga" hist_tem 12 "pegou carga na Baia 4 (tarefa 55)"
espera 3 "robô 13 pegou a carga" hist_tem 13 "pegou carga na Baia 4 (tarefa 55)"
kill -KILL "$(pid_robo 13)"
sleep 0.5

echo "8. Encerramento ordenado"
kill -TERM $PID_COLETOR
wait $PID_COLETOR
[ $? -eq 0 ] && ok "Coletor saiu com código 0" || falha "código de saída do Coletor"
cargas_tem ";55;Baia 4;-;12+13;.*;incompleta$" \
    && ok "carga 55 gravada como incompleta (robô morreu no caminho)" || falha "carga incompleta não registrada"
grep -q "encerrado: .* 0 erros de gravação" $LOGS/coletor2.log \
    && ok "encerrou sem erros de gravação" || falha "erros de gravação (ver log)"
grep -q "perdas: 0 no buffer compartilhado, 0 na fila interna" $LOGS/coletor2.log \
    && ok "nenhuma perda no buffer compartilhado nem na fila interna" || falha "houve perda de eventos (ver log)"
grep "encerrado:" $LOGS/coletor2.log | sed 's/^[^]]*\] //'

kill -TERM $PID_GESTOR
wait $PID_GESTOR
kill $PID_ESCUTA 2>/dev/null
wait $PID_ESCUTA 2>/dev/null

echo
echo "Logs em $LOGS/ e dados em $DADOS/"
if [ $falhas -eq 0 ]; then echo "TODOS OS TESTES PASSARAM"; else echo "$falhas TESTE(S) FALHARAM"; fi
exit $falhas
