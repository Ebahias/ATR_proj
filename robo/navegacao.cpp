/*
 * Thread de Comando de Navegação (periódica, T1 = PERIODO_NAVEGACAO_MS).
 *
 * A cada ciclo:
 *   1. esvazia a fila de comandos do robô (sem bloquear);
 *   2. escolhe o setpoint (destino forçado pelo Status > destino do comando)
 *      e o escreve no hardware, a menos que o Status tenha bloqueado o movimento;
 *   3. detecta a chegada ao destino.
 *
 * Tarefa 3 (a fazer): rota pelo mapa em vez de linha reta, passagem segura
 * pelas passagens estreitas e sequência da tarefa de transporte.
 */

#include "robo/threads_robo.hpp"
#include "robo/config_robo.hpp"
#include "robo/contexto_robo.hpp"
#include "comum/log.hpp"
#include "comum/periodico.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>


// Em automático só o Organizador comanda o movimento; em manual, só o operador.
static bool origem_permitida(int modo, int origem) {
    return (modo == MODO_AUTOMATICO) == (origem == ORIGEM_ORGANIZADOR);
}

static void tratar_comando(ContextoRobo* ctx, const ComandoRobo& cmd, const char* modulo) {

    int modo = ctx->modo();
    int estado = ctx->estado();

    switch(cmd.tipo) {

        case CMD_IR_PARA:
        case CMD_ACOPLAR_E_ERGUER:
        case CMD_SOLTAR_CARGA:
            if(!origem_permitida(modo, cmd.origem)) {
                log_msg(modulo, "%s ignorado: origem %s em modo %s", tipo_comando_nome(cmd.tipo),
                        cmd.origem == ORIGEM_OPERADOR ? "operador" : "organizador", modo_robo_nome(modo));
                return;
            }
            if(estado == ESTADO_DEFEITO) {
                log_msg(modulo, "%s ignorado: robô com defeito", tipo_comando_nome(cmd.tipo));
                return;
            }
            break;

        default:
            break;
    }

    shm_evento_publicar(ctx->shm, ctx->id, EV_COMANDO, "%s (%d, %d) valor=%d tarefa=%d origem=%s",
                        tipo_comando_nome(cmd.tipo), cmd.x, cmd.y, cmd.valor, cmd.tarefa_id,
                        cmd.origem == ORIGEM_OPERADOR ? "operador" : "organizador");

    switch(cmd.tipo) {

        case CMD_IR_PARA:
            // TODO tarefa 3: planejar a rota até (x, y) em vez de ir em linha reta
            ctx->definir_destino(cmd.x, cmd.y);
            ctx->mudar_estado(ESTADO_DESLOCANDO, "comando IR_PARA");
            break;

        case CMD_PARAR: {
            LeituraHardware l = ctx->hw->ler();
            ctx->definir_destino(l.pos_x, l.pos_y);
            if(estado == ESTADO_DESLOCANDO)
                ctx->mudar_estado(ESTADO_DISPONIVEL, "comando PARAR");
            break;
        }

        case CMD_MODO:
            if(cmd.valor == MODO_AUTOMATICO || cmd.valor == MODO_MANUAL)
                ctx->mudar_modo(cmd.valor);
            break;

        case CMD_ACOPLAR_E_ERGUER:
            ctx->hw->acoplar_e_erguer(true);
            ctx->mudar_estado(ESTADO_OPERANDO_CARGA, "comando ACOPLAR_E_ERGUER");
            break;

        case CMD_SOLTAR_CARGA:
            ctx->hw->acoplar_e_erguer(false);
            ctx->mudar_estado(ESTADO_DISPONIVEL, "comando SOLTAR_CARGA");
            break;

        case CMD_SIM_FALHA:
            ctx->hw->simular_falha(cmd.valor != 0);
            break;

        case CMD_SIM_BATERIA:
            ctx->hw->simular_bateria(cmd.valor);
            break;

        default:
            log_msg(modulo, "comando desconhecido %d ignorado", cmd.tipo);
            break;
    }
}

static void atualizar_movimento(ContextoRobo* ctx) {

    InstantaneoRobo s = ctx->instantaneo();
    LeituraHardware l = ctx->hw->ler();

    if(s.movimento_bloqueado) {
        ctx->hw->escrever_setpoint(l.pos_x, l.pos_y);
        return;
    }

    int x, y;

    if(s.tem_destino_forcado) {
        x = s.forcado_x;
        y = s.forcado_y;
    } else if(s.tem_destino) {
        x = s.destino_x;
        y = s.destino_y;
    } else {
        return;
    }

    // TODO tarefa 3: o destino final vira uma sequência de waypoints, e a
    // entrada em passagens estreitas é controlada (ex.: semáforo nomeado
    // sem_open("/atr_passagem_<n>") com valor inicial = capacidade).
    ctx->hw->escrever_setpoint(x, y);

    if(s.estado == ESTADO_DESLOCANDO && !s.tem_destino_forcado && l.pos_x == x && l.pos_y == y) {
        ctx->mudar_estado(ESTADO_DISPONIVEL, "chegou ao destino");
        shm_evento_publicar(ctx->shm, ctx->id, EV_CHEGOU_DESTINO, "chegou em (%d, %d)", x, y);
    }
}

void* thread_navegacao(void* arg) {

    ContextoRobo* ctx = (ContextoRobo*)arg;

    char modulo[32];
    snprintf(modulo, sizeof(modulo), "ROBO %d NAV", ctx->id);

    Periodo periodo(PERIODO_NAVEGACAO_MS);

    while(!ctx->deve_encerrar()) {

        ComandoRobo cmd;
        ssize_t n;

        while((n = mq_receive(ctx->mq_comando, (char*)&cmd, sizeof(cmd), NULL)) == (ssize_t)sizeof(cmd))
            tratar_comando(ctx, cmd, modulo);

        if(n < 0 && errno != EAGAIN)
            log_msg(modulo, "mq_receive: %s", strerror(errno));

        atualizar_movimento(ctx);

        periodo.esperar();
    }

    log_msg(modulo, "encerrada: %lu ciclos, %lu atrasos, pior execução %ld us",
            periodo.ciclos(), periodo.atrasos(), periodo.max_exec_us());

    return NULL;
}
