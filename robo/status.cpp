/*
 * Thread de Status do Robô (periódica, T2 = PERIODO_STATUS_MS).
 *
 * A cada ciclo lê o hardware, avalia as condições do robô e publica o status
 * no slot do robô na memória compartilhada da frota, onde o Organizador lê.
 *
 * Tarefa 4 (a fazer, em avaliar_condicoes): detectar falha, bateria
 * fraca/recarga e calcular a distância total percorrida.
 */

#include "robo/threads_robo.hpp"
#include "robo/config_robo.hpp"
#include "robo/contexto_robo.hpp"
#include "comum/log.hpp"
#include "comum/periodico.hpp"

#include <cstdio>
#include <unistd.h>


// Ponto de extensão da tarefa 4. Ferramentas disponíveis no contexto:
//   ctx->mudar_estado(ESTADO_DEFEITO, ...) / ctx->bloquear_movimento(true)
//   ctx->forcar_destino(x, y) / ctx->liberar_destino_forcado()
//   ctx->definir_distancia_total(d)
//   shm_evento_publicar(ctx->shm, ctx->id, EV_DEFEITO / EV_RECARGA, ...)
static void avaliar_condicoes(ContextoRobo* ctx, const LeituraHardware& atual, const LeituraHardware& anterior) {

    (void)ctx;
    (void)atual;
    (void)anterior;

    // TODO tarefa 4: falha -> DEFEITO + bloquear movimento
    // TODO tarefa 4: bateria baixa -> destino forçado para recarga, RECARREGANDO
    // TODO tarefa 4: distância total += |atual - anterior|
}

static void publicar(ContextoRobo* ctx, const LeituraHardware& l) {

    InstantaneoRobo s = ctx->instantaneo();

    DadosStatus d;
    d.ativo = 1;
    d.robo_id = ctx->id;
    d.pid = getpid();
    d.estado = s.estado;
    d.modo = s.modo;
    d.pos_x = l.pos_x;
    d.pos_y = l.pos_y;

    if(s.tem_destino_forcado) {
        d.sp_x = s.forcado_x;
        d.sp_y = s.forcado_y;
    } else if(s.tem_destino) {
        d.sp_x = s.destino_x;
        d.sp_y = s.destino_y;
    } else {
        d.sp_x = l.pos_x;
        d.sp_y = l.pos_y;
    }

    d.bateria = l.bateria;
    d.carga_atracada = l.carga_atracada;
    d.falha = l.falha;
    d.distancia_total = s.distancia_total;

    shm_slot_publicar(ctx->shm, ctx->slot, d);
}

void* thread_status(void* arg) {

    ContextoRobo* ctx = (ContextoRobo*)arg;

    char modulo[32];
    snprintf(modulo, sizeof(modulo), "ROBO %d STATUS", ctx->id);

    Periodo periodo(PERIODO_STATUS_MS);

    LeituraHardware anterior = ctx->hw->ler();

    while(!ctx->deve_encerrar()) {

        LeituraHardware atual = ctx->hw->ler();

        avaliar_condicoes(ctx, atual, anterior);

        publicar(ctx, atual);

        anterior = atual;

        periodo.esperar();
    }

    log_msg(modulo, "encerrada: %lu ciclos, %lu atrasos, pior execução %ld us",
            periodo.ciclos(), periodo.atrasos(), periodo.max_exec_us());

    return NULL;
}
