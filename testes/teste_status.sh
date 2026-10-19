#!/usr/bin/env bash
# Teste de integração do Status do Robô (tarefa 4): defeito, bateria/recarga
# e distância percorrida.
# Executar a partir da raiz do projeto:  make teste-status

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_status
FIFO=$LOGS/novo_robo.fifo
mkdir -p "$LOGS"
rm -f "$LOGS"/*.log "$FIFO" /dev/mqueue/atr_* /dev/shm/atr_frota

falhas=0
ok()    { echo "  [OK]    $1"; }
falha() { echo "  [FALHA] $1"; falhas=$((falhas + 1)); }

# campo <id> <nome>: coluna da tabela de status, contando do fim da linha
# (o estado pode ter espaço). Nomes: pos sp bat carga falha dist fase tarefa
campo() {
    bin/monitor_frota status | awk -v id="$1" -v c="$2" 'NR > 1 && $2 == id {
        m["fase"] = $(NF-3); m["tarefa"] = $(NF-2); m["dist"] = $(NF-5); m["falha"] = $(NF-6);
        m["carga"] = $(NF-7); m["bat"] = $(NF-8); m["sp"] = $(NF-9); m["pos"] = $(NF-10);
        print m[c] }'
}

estado() { bin/monitor_frota status | awk -v id="$1" 'NR > 1 && $2 == id' | grep -oE "disponível|deslocando|operando carga|recarregando|defeito"; }

espera() {
    local seg=$1 desc=$2; shift 2
    for _ in $(seq $((seg * 10))); do
        if "$@"; then ok "$desc"; return 0; fi
        sleep 0.1
    done
    falha "$desc"
    return 1
}

campo_eh()   { [ "$(campo "$1" "$2")" = "$3" ]; }
estado_eh()  { [ "$(estado "$1")" = "$2" ]; }
evento_tem() { grep -q -- "$1" $LOGS/eventos.log; }
bat_maior()  { [ "$(campo "$1" bat | tr -d '%')" -ge "$2" ]; }
vaga_de()    { bin/monitor_frota recarga | grep -c "robo=$1$"; }
# Condições como funções: "espera" reavalia a cada tentativa
vagas_de_eh()     { [ "$(vaga_de "$1")" -eq "$2" ]; }
vagas_ocupadas_eh() { [ "$(bin/monitor_frota recarga | grep -vc "robo=-")" -eq "$1" ]; }

bin/gestor_robos --fifo "$FIFO" > $LOGS/gestor.log 2>&1 &
PID_GESTOR=$!
sleep 0.3
bin/monitor_frota eventos > $LOGS/eventos.log 2>&1 &
PID_EVENTOS=$!

echo "1. Distância total percorrida"
bin/novo_robo 1 50 15 --fifo "$FIFO"
espera 3 "robô 1 pronto" estado_eh 1 "disponível"
bin/comando_robo 1 ir 100 15
espera 5 "robô 1 chegou em (100,15)" campo_eh 1 pos "(100,15)"
sleep 0.3
d=$(campo 1 dist)
awk -v d="$d" 'BEGIN { exit !(d >= 49 && d <= 51) }' && ok "distância = $d (esperado 50)" || falha "distância $d (esperado 50)"
bin/comando_robo 1 ir 50 15
espera 5 "robô 1 voltou" campo_eh 1 pos "(50,15)"
sleep 0.3
d=$(campo 1 dist)
awk -v d="$d" 'BEGIN { exit !(d >= 99 && d <= 101) }' && ok "distância acumulada = $d (esperado 100)" || falha "distância $d (esperado 100)"

echo "2. Defeito durante o deslocamento"
bin/novo_robo 2 50 105 --fifo "$FIFO"
espera 3 "robô 2 pronto" estado_eh 2 "disponível"
bin/comando_robo 2 ir 150 105
sleep 0.5
bin/comando_robo 2 falha 1
espera 2 "estado defeito" estado_eh 2 "defeito"
p1=$(campo 2 pos); sleep 0.5; p2=$(campo 2 pos)
[ "$p1" = "$p2" ] && ok "robô parou em $p1" || falha "robô andou com defeito ($p1 -> $p2)"
evento_tem "robo=2 DEFEITO: defeito presente: falha no hardware" && ok "evento 'defeito presente'" || falha "evento de defeito"
bin/comando_robo 2 ir 50 105
sleep 0.3
grep -q "ROBO 2 NAV\] IR_PARA ignorado: robô com defeito" $LOGS/gestor.log \
    && ok "comando de movimento recusado com defeito" || falha "comando aceito com defeito"
bin/comando_robo 2 falha 0
espera 2 "defeito removido, volta a deslocando" estado_eh 2 "deslocando"
espera 5 "robô 2 retomou e chegou ao destino" campo_eh 2 pos "(150,105)"
espera 2 "robô 2 disponível" estado_eh 2 "disponível"

echo "3. Bateria baixa: vai recarregar e fica indisponível"
bin/novo_robo 3 50 105 --fifo "$FIFO"
espera 3 "robô 3 pronto" estado_eh 3 "disponível"
bin/comando_robo 3 bateria 15
espera 2 "estado recarregando" estado_eh 3 "recarregando"
espera 2 "vaga de recarga reservada" vagas_de_eh 3 1
espera 5 "robô 3 chegou na vaga da Recarga 2" campo_eh 3 pos "(94,90)"
evento_tem "robo=3 RECARGA: em recarga na Recarga 2" && ok "evento 'em recarga'" || falha "evento em recarga"
bin/comando_robo 3 tarefa 7 15 105 185 105
sleep 0.3
estado_eh 3 "recarregando" && ok "continua indisponível ao receber tarefa" || falha "saiu de recarregando"
espera 8 "recarga concluída (bateria >= 95%)" estado_eh 3 "deslocando"
bat_maior 3 95 && ok "bateria $(campo 3 bat)" || falha "bateria $(campo 3 bat)"
[ "$(vaga_de 3)" -eq 0 ] && ok "vaga de recarga liberada" || falha "vaga não liberada"
espera 10 "robô 3 retomou a tarefa recebida durante a recarga" campo_eh 3 fase "aguardando_equipe"

echo "4. Disputa pelas vagas de recarga (4 robôs, 3 vagas na Recarga 1)"
for i in 4 5 6 7; do bin/novo_robo $i 100 15 --fifo "$FIFO"; done
espera 3 "robôs 4-7 prontos" estado_eh 7 "disponível"
for i in 4 5 6 7; do bin/comando_robo $i bateria 15; done
espera 3 "4 vagas reservadas, uma por robô" vagas_ocupadas_eh 4
[ "$(bin/monitor_frota recarga | grep "recarga=1" | grep -vc "robo=-")" -eq 3 ] \
    && ok "Recarga 1 lotada (3 vagas), quarto robô foi para a Recarga 2" || falha "distribuição das vagas"
for i in 4 5 6 7; do espera 10 "robô $i recarregou" estado_eh $i "disponível"; done

echo "5. Recarga adiada enquanto carrega uma caixa"
bin/novo_robo 8 15 15 --fifo "$FIFO"
espera 3 "robô 8 pronto" estado_eh 8 "disponível"
bin/comando_robo 8 tarefa 9 15 15 185 15
espera 3 "robô 8 na coleta" campo_eh 8 fase "aguardando_equipe"
bin/comando_robo 8 acoplar
espera 3 "robô 8 transportando" campo_eh 8 fase "transportando"
bin/comando_robo 8 bateria 15
espera 2 "recarga adiada" evento_tem "robo=8 RECARGA: bateria 1[0-9]%: recarga adiada até entregar a carga"
estado_eh 8 "operando carga" && ok "continua operando a carga" || falha "abandonou a carga"
espera 10 "entregou a carga" campo_eh 8 fase "concluida"
espera 3 "depois da entrega, vai recarregar" estado_eh 8 "recarregando"

echo "6. Modo manual: só avisa, o operador decide"
bin/novo_robo 9 50 45 --fifo "$FIFO"
espera 3 "robô 9 pronto" estado_eh 9 "disponível"
bin/comando_robo 9 manual --operador
espera 2 "robô 9 em manual" campo_eh 9 fase "-"
sleep 0.2
bin/comando_robo 9 bateria 15
espera 2 "aviso de bateria baixa" evento_tem "robo=9 RECARGA: bateria 1[0-9]%: recarga necessária (modo manual"
estado_eh 9 "disponível" && ok "não foi forçado a recarregar" || falha "forçado a recarregar em manual"
bin/comando_robo 9 ir 100 30 --operador
espera 5 "operador levou o robô à recarga" campo_eh 9 pos "(100,30)"
espera 5 "bateria carrega na estação" bat_maior 9 50

echo "7. Bateria esgotada = defeito"
bin/novo_robo 10 50 75 --fifo "$FIFO"
espera 3 "robô 10 pronto" estado_eh 10 "disponível"
bin/comando_robo 10 bateria 0
espera 2 "estado defeito" estado_eh 10 "defeito"
evento_tem "robo=10 DEFEITO: defeito presente: bateria esgotada" && ok "evento 'bateria esgotada'" || falha "evento bateria esgotada"

echo "8. Robô morre com vaga de recarga reservada"
bin/novo_robo 11 100 105 --fifo "$FIFO"
espera 3 "robô 11 pronto" estado_eh 11 "disponível"
bin/comando_robo 11 bateria 15
espera 2 "robô 11 com vaga" vagas_de_eh 11 1
kill -KILL "$(bin/monitor_frota status | awk '$2 == 11 {print $3}')"
espera 3 "Gestor liberou a vaga" vagas_de_eh 11 0

kill -TERM $PID_GESTOR
wait $PID_GESTOR
grep -E "(NAV|STATUS)\] encerrada: .* [1-9][0-9]* atrasos" $LOGS/gestor.log >/dev/null \
    && falha "thread perdeu prazo (ver log)" || ok "threads periódicas sem perda de prazo"

kill $PID_EVENTOS 2>/dev/null
wait $PID_EVENTOS 2>/dev/null

echo
echo "Logs em $LOGS/"
if [ $falhas -eq 0 ]; then echo "TODOS OS TESTES PASSARAM"; else echo "$falhas TESTE(S) FALHARAM"; fi
exit $falhas
