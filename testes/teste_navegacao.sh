#!/usr/bin/env bash
# Teste de integração do Comando de Navegação (tarefa 3): rotas, passagens
# estreitas e transporte em equipe.
# Executar a partir da raiz do projeto:  make teste-navegacao

cd "$(dirname "$0")/.." || exit 1

LOGS=run/teste_navegacao
FIFO=$LOGS/novo_robo.fifo
mkdir -p "$LOGS"
rm -f "$LOGS"/*.log "$FIFO" /dev/mqueue/atr_* /dev/shm/atr_frota

falhas=0
ok()    { echo "  [OK]    $1"; }
falha() { echo "  [FALHA] $1"; falhas=$((falhas + 1)); }

# campo <id> <nome>: coluna da tabela de status (contando do fim da linha,
# porque o estado pode ter espaço). Nomes: pos sp bat carga fase tarefa pass fila
campo() {
    bin/monitor_frota status | awk -v id="$1" -v c="$2" 'NR > 1 && $2 == id {
        m["fila"] = $NF; m["pass"] = $(NF-1); m["tarefa"] = $(NF-2); m["fase"] = $(NF-3);
        m["carga"] = $(NF-7); m["bat"] = $(NF-8); m["sp"] = $(NF-9); m["pos"] = $(NF-10);
        print m[c] }'
}

# espera <segundos> <descrição> <comando...>
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
evento_tem() { grep -q -- "$1" $LOGS/eventos.log; }

bin/gestor_robos --fifo "$FIFO" > $LOGS/gestor.log 2>&1 &
PID_GESTOR=$!
sleep 0.3
bin/monitor_frota eventos > $LOGS/eventos.log 2>&1 &
PID_EVENTOS=$!

# Amostra a ocupação das passagens durante todo o teste
( while true; do bin/monitor_frota passagens; sleep 0.05; done ) > $LOGS/passagens.log 2>&1 &
PID_AMOSTRAS=$!

echo "1. Rotas do mapa"
bin/mapa rota 15 45 185 45 1 | grep -q "P0 cap 1" \
    && ok "robô sozinho usa o atalho de capacidade 1" || falha "rota solo"
! bin/mapa rota 15 75 185 75 3 | grep -q "P1 cap 2" \
    && ok "equipe de 3 evita a passagem de capacidade 2" || falha "rota de equipe de 3"
! bin/mapa rota 15 15 185 105 4 | grep -q "=\[P" \
    && ok "equipe de 4 só usa corredores largos" || falha "rota de equipe de 4"

echo "2. Passagem de capacidade 1 (3 robôs ao mesmo tempo)"
for i in 1 2 3; do bin/novo_robo $i 50 45 --fifo "$FIFO"; done
espera 3 "robôs 1-3 prontos" campo_eh 3 fase "-"
for i in 1 2 3; do bin/comando_robo $i ir 150 45; done
espera 3 "um robô espera vaga no corredor central norte" evento_tem "aguardando vaga: Corredor central norte"
for i in 1 2 3; do espera 15 "robô $i atravessou e chegou em (150,45)" campo_eh $i pos "(150,45)"; done

echo "3. Passagem de capacidade 2 (4 robôs ao mesmo tempo)"
for i in 4 5 6 7; do bin/novo_robo $i 50 75 --fifo "$FIFO"; done
espera 3 "robôs 4-7 prontos" campo_eh 7 fase "-"
for i in 4 5 6 7; do bin/comando_robo $i ir 150 75; done
for i in 4 5 6 7; do espera 15 "robô $i atravessou e chegou em (150,75)" campo_eh $i pos "(150,75)"; done
grep -q "^P1 cap=2 ocupacao=2 " $LOGS/passagens.log \
    && ok "dois robôs dentro da passagem ao mesmo tempo" || falha "capacidade 2 não foi usada"

echo "4. Transporte em equipe: Baia 3 -> Zona C (robôs 8 e 9)"
bin/novo_robo 8 100 90 --fifo "$FIFO"
bin/novo_robo 9 100 90 --fifo "$FIFO"
espera 3 "robôs 8 e 9 prontos" campo_eh 9 fase "-"
bin/comando_robo 8 tarefa 42 12 75 182 75 --equipe 8,9
bin/comando_robo 9 tarefa 42 18 75 188 75 --equipe 8,9
espera 15 "robô 8 na coleta, aguardando a equipe" campo_eh 8 fase "aguardando_equipe"
espera 15 "robô 9 na coleta, aguardando a equipe" campo_eh 9 fase "aguardando_equipe"
sleep 0.5
campo_eh 8 fase "aguardando_equipe" && ok "sem liberação do Organizador, a equipe não sai" || falha "equipe saiu sozinha"
bin/comando_robo 8 acoplar
bin/comando_robo 9 acoplar
espera 3 "equipe atracada e transportando" campo_eh 9 fase "transportando"
# Durante o transporte, a distância entre os dois robôs fica pequena (barreira)
maxdist=0
for _ in $(seq 60); do
    p8=$(campo 8 pos); p9=$(campo 9 pos)
    d=$(echo "$p8 $p9" | tr -d '()' | tr ',' ' ' | awk '{dx=$1-$3; dy=$2-$4; print int(sqrt(dx*dx+dy*dy))}')
    [ "$d" -gt "$maxdist" ] && maxdist=$d
    [ "$(campo 8 fase)" = "concluida" ] && break
    sleep 0.1
done
[ "$maxdist" -le 30 ] && ok "equipe andou junta (distância máxima $maxdist)" || falha "equipe se separou (distância $maxdist)"
espera 15 "robô 8 entregou (concluída)" campo_eh 8 fase "concluida"
espera 3 "robô 9 entregou (concluída)" campo_eh 9 fase "concluida"
campo_eh 8 pos "(182,75)" && campo_eh 9 pos "(188,75)" \
    && ok "robôs nos pontos de entrega na Zona C" || falha "posições de entrega"
campo_eh 8 carga 0 && campo_eh 9 carga 0 && ok "carga solta" || falha "carga ainda atracada"
grep -q "^P1 cap=2 ocupacao=2 dentro=8,9 " $LOGS/passagens.log \
    && ok "equipe atravessou a passagem junta (reserva)" || falha "equipe não atravessou junta"
evento_tem "robo=8 CARGA: pegou carga na Baia 3 (tarefa 42)" \
    && evento_tem "robo=9 CARGA: entregou carga na Zona C (tarefa 42)" \
    && ok "eventos de rastreabilidade (pegou/entregou)" || falha "eventos de carga"

echo "5. Robô morre dentro da passagem de capacidade 1"
bin/novo_robo 10 50 45 --fifo "$FIFO"
bin/novo_robo 11 50 45 --fifo "$FIFO"
espera 3 "robôs 10 e 11 prontos" campo_eh 11 fase "-"
bin/comando_robo 10 ir 150 45
espera 3 "robô 10 dentro da passagem" campo_eh 10 pass 0
bin/comando_robo 11 ir 150 45
espera 3 "robô 11 na fila da passagem" campo_eh 11 fila 0
kill -KILL "$(bin/monitor_frota status | awk '$2 == 10 {print $3}')"
espera 5 "Gestor liberou a vaga e o robô 11 atravessou" campo_eh 11 pos "(150,45)"

echo "6. PARAR dentro da passagem: termina a travessia e libera a vaga"
bin/comando_robo 11 ir 50 45
espera 3 "robô 11 dentro da passagem" campo_eh 11 pass 0
bin/comando_robo 11 parar
espera 5 "robô 11 parou na saída da passagem" campo_eh 11 pos "(70,45)"
espera 2 "vaga liberada" campo_eh 11 pass -1

echo "7. Invariante: ocupação nunca passou da capacidade"
kill $PID_AMOSTRAS 2>/dev/null
wait $PID_AMOSTRAS 2>/dev/null
amostras=$(grep -c "^P" $LOGS/passagens.log)
excessos=$(awk '/^P/ { split($2, c, "="); split($3, o, "="); if (o[2] + 0 > c[2] + 0) n++ } END { print n + 0 }' $LOGS/passagens.log)
[ "$excessos" -eq 0 ] && ok "$amostras amostras, nenhuma acima da capacidade" || falha "$excessos amostras acima da capacidade"

kill -TERM $PID_GESTOR
wait $PID_GESTOR
grep -q "NAV\] encerrada: .* [1-9][0-9]* atrasos" $LOGS/gestor.log \
    && falha "thread de navegação perdeu prazo (ver log)" || ok "threads de navegação sem perda de prazo"

kill $PID_EVENTOS 2>/dev/null
wait $PID_EVENTOS 2>/dev/null

echo
echo "Logs em $LOGS/"
if [ $falhas -eq 0 ]; then echo "TODOS OS TESTES PASSARAM"; else echo "$falhas TESTE(S) FALHARAM"; fi
exit $falhas
