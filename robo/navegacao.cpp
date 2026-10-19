/*
 * Thread de Comando de Navegação (periódica, T1 = PERIODO_NAVEGACAO_MS).
 *
 * A cada ciclo:
 *   1. esvazia a fila de comandos do robô (sem bloquear) e repassa cada
 *      comando ao Navegador;
 *   2. executa um ciclo do Navegador (rota, passagens estreitas, tarefa de
 *      transporte; ver robo/navegador.hpp).
 */

#include "robo/threads_robo.hpp"
#include "robo/config_robo.hpp"
#include "robo/contexto_robo.hpp"
#include "robo/navegador.hpp"
#include "comum/log.hpp"
#include "comum/periodico.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>


// Em automático só o Organizador comanda o movimento; em manual, só o operador.
static bool origem_permitida(int modo, int origem) {
    return (modo == MODO_AUTOMATICO) == (origem == ORIGEM_ORGANIZADOR);
}

static void tratar_comando(ContextoRobo* ctx, Navegador& nav, const ComandoRobo& cmd, const char* modulo) {

    int modo = ctx->modo();

    switch(cmd.tipo) {

        case CMD_IR_PARA:
        case CMD_ACOPLAR_E_ERGUER:
        case CMD_SOLTAR_CARGA:
        case CMD_TAREFA:
            if(!origem_permitida(modo, cmd.origem)) {
                log_msg(modulo, "%s ignorado: origem %s em modo %s", tipo_comando_nome(cmd.tipo),
                        cmd.origem == ORIGEM_OPERADOR ? "operador" : "organizador", modo_robo_nome(modo));
                return;
            }
            if(ctx->estado() == ESTADO_DEFEITO) {
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
            nav.ir_para(cmd.x, cmd.y);
            break;

        case CMD_PARAR:
            nav.parar();
            break;

        case CMD_MODO:
            // Ao trocar de modo o robô para e espera ordens de quem passa a comandar
            if((cmd.valor == MODO_AUTOMATICO || cmd.valor == MODO_MANUAL) && cmd.valor != modo) {
                nav.parar();
                ctx->mudar_modo(cmd.valor);
            }
            break;

        case CMD_TAREFA:
            nav.iniciar_tarefa(cmd);
            break;

        case CMD_ACOPLAR_E_ERGUER:
            nav.acoplar();
            break;

        case CMD_SOLTAR_CARGA:
            nav.soltar();
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

void* thread_navegacao(void* arg) {

    ContextoRobo* ctx = (ContextoRobo*)arg;

    char modulo[32];
    snprintf(modulo, sizeof(modulo), "ROBO %d NAV", ctx->id);

    Navegador nav(ctx);
    Periodo periodo(PERIODO_NAVEGACAO_MS);

    while(!ctx->deve_encerrar()) {

        ComandoRobo cmd;
        ssize_t n;

        while((n = mq_receive(ctx->mq_comando, (char*)&cmd, sizeof(cmd), NULL)) == (ssize_t)sizeof(cmd))
            tratar_comando(ctx, nav, cmd, modulo);

        if(n < 0 && errno != EAGAIN)
            log_msg(modulo, "mq_receive: %s", strerror(errno));

        nav.ciclo();

        periodo.esperar();
    }

    nav.liberar();

    log_msg(modulo, "encerrada: %lu ciclos, %lu atrasos, pior execução %ld us",
            periodo.ciclos(), periodo.atrasos(), periodo.max_exec_us());

    return NULL;
}
