/*
 * Thread de Status do Robô (tarefa 4; periódica, T2 = PERIODO_STATUS_MS).
 *
 * A cada ciclo:
 *   1. lê o hardware e acumula a distância total percorrida;
 *   2. detecta defeito (i_falha ou bateria esgotada): estado DEFEITO e
 *      movimento bloqueado na navegação; ao sumir, volta ao estado anterior;
 *   3. verifica a bateria: abaixo de BATERIA_MINIMA reserva uma vaga de
 *      recarga (comum/recarga.hpp) e força a navegação a ir até ela; o robô
 *      fica RECARREGANDO (indisponível) até BATERIA_RECARREGADA e depois
 *      retoma o que estava fazendo;
 *   4. publica o status no slot do robô na memória compartilhada.
 *
 * Decisões de projeto:
 *  - Robô carregando uma caixa (fases ACOPLANDO a ENTREGANDO) não abandona a
 *    equipe: a recarga é adiada até a entrega (a bateria mínima dá folga
 *    para terminar o transporte).
 *  - Em modo manual, o operador decide: o Status só avisa da bateria baixa.
 *  - Depois de defeito ou recarga, o estado volta ao que a navegação quer
 *    (InfoNavegacao::estado_desejado), não a uma cópia antiga: um comando
 *    recebido durante a recarga é retomado com o estado certo.
 */

#include "robo/threads_robo.hpp"
#include "robo/config_robo.hpp"
#include "robo/contexto_robo.hpp"
#include "comum/log.hpp"
#include "comum/periodico.hpp"
#include "comum/recarga.hpp"

#include <cmath>
#include <cstdio>
#include <unistd.h>


enum EtapaRecarga {
    RECARGA_NAO,
    RECARGA_SEM_VAGA,           // todas as vagas ocupadas: espera na estação mais próxima
    RECARGA_INDO,               // vaga reservada, indo até ela
    RECARGA_CARREGANDO
};

struct MonitorStatus {
    ContextoRobo* ctx;
    char modulo[32];
    double distancia;
    bool em_defeito;
    EtapaRecarga recarga;
    int vaga;
    bool recarga_adiada;
    bool aviso_manual;
};

static bool carregando_caixa(int fase) {
    return fase >= FASE_ACOPLANDO && fase <= FASE_ENTREGANDO;
}

static void verificar_defeito(MonitorStatus& m, const LeituraHardware& l, const InstantaneoRobo& s) {

    ContextoRobo* ctx = m.ctx;
    bool defeito = l.falha || l.bateria <= 0;

    if(defeito && !m.em_defeito) {

        m.em_defeito = true;

        const char* causa = l.falha ? "falha no hardware" : "bateria esgotada";

        ctx->bloquear_movimento(true);
        ctx->mudar_estado(ESTADO_DEFEITO, causa);
        shm_evento_publicar(ctx->shm, ctx->id, EV_DEFEITO, "defeito presente: %s em (%d, %d)",
                            causa, l.pos_x, l.pos_y);

    } else if(!defeito && m.em_defeito) {

        m.em_defeito = false;

        ctx->bloquear_movimento(false);
        ctx->mudar_estado(m.recarga != RECARGA_NAO ? ESTADO_RECARREGANDO : s.nav.estado_desejado,
                          "defeito removido");
        shm_evento_publicar(ctx->shm, ctx->id, EV_DEFEITO, "defeito removido");
    }
}

static void ir_para_vaga(MonitorStatus& m, const LeituraHardware& l) {

    ContextoRobo* ctx = m.ctx;
    int vaga = recarga_reservar(ctx->shm, ctx->slot, l.pos_x, l.pos_y);

    if(vaga >= 0) {

        const VagaRecargaMapa& v = mapa_vaga_recarga(vaga);

        m.vaga = vaga;
        m.recarga = RECARGA_INDO;
        ctx->forcar_destino(v.x, v.y);
        shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA, "bateria %d%%: indo para Recarga %d (%d, %d)",
                            l.bateria, v.recarga, v.x, v.y);
        return;
    }

    if(m.recarga != RECARGA_SEM_VAGA) {

        // Sem vaga: espera na entrada da estação mais próxima e tenta de novo a cada ciclo
        const AreaMapa* mais_proxima = NULL;
        double melhor = 0.0;

        for(int i=0;i<mapa_num_areas();i++) {
            const AreaMapa& a = mapa_area(i);
            double d = std::hypot((double)(a.cx - l.pos_x), (double)(a.cy - l.pos_y));
            if(a.tipo == AREA_RECARGA && (!mais_proxima || d < melhor)) {
                mais_proxima = &a;
                melhor = d;
            }
        }

        const NoMapa& acesso = mapa_no(mais_proxima->no_acesso);

        m.recarga = RECARGA_SEM_VAGA;
        ctx->forcar_destino(acesso.x, acesso.y);
        shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA, "bateria %d%%: aguardando vaga em %s",
                            l.bateria, mais_proxima->nome);
    }
}

static void verificar_bateria(MonitorStatus& m, const LeituraHardware& l, const InstantaneoRobo& s) {

    ContextoRobo* ctx = m.ctx;

    switch(m.recarga) {

        case RECARGA_NAO:

            if(l.bateria > BATERIA_MINIMA) {
                m.recarga_adiada = false;
                m.aviso_manual = false;
                break;
            }

            if(s.modo == MODO_MANUAL) {
                if(!m.aviso_manual) {
                    m.aviso_manual = true;
                    shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA,
                                        "bateria %d%%: recarga necessária (modo manual, decisão do operador)", l.bateria);
                }
                break;
            }

            if(carregando_caixa(s.nav.fase)) {
                if(!m.recarga_adiada) {
                    m.recarga_adiada = true;
                    shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA,
                                        "bateria %d%%: recarga adiada até entregar a carga (tarefa %d)",
                                        l.bateria, s.nav.tarefa_id);
                }
                break;
            }

            m.recarga_adiada = false;
            ctx->mudar_estado(ESTADO_RECARREGANDO, "bateria baixa");
            ir_para_vaga(m, l);
            break;

        case RECARGA_SEM_VAGA:
            ir_para_vaga(m, l);
            break;

        case RECARGA_INDO: {
            const VagaRecargaMapa& v = mapa_vaga_recarga(m.vaga);
            if(l.pos_x == v.x && l.pos_y == v.y) {
                m.recarga = RECARGA_CARREGANDO;
                shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA, "em recarga na Recarga %d (bateria %d%%)",
                                    v.recarga, l.bateria);
            }
            break;
        }

        case RECARGA_CARREGANDO:
            if(l.bateria >= BATERIA_RECARREGADA) {
                recarga_liberar(ctx->shm, ctx->slot);
                ctx->liberar_destino_forcado();
                m.recarga = RECARGA_NAO;
                m.vaga = -1;
                ctx->mudar_estado(s.nav.estado_desejado, "recarga concluída");
                shm_evento_publicar(ctx->shm, ctx->id, EV_RECARGA, "recarga concluída (bateria %d%%)", l.bateria);
            }
            break;
    }
}

static void avaliar_condicoes(MonitorStatus& m, const LeituraHardware& atual, const LeituraHardware& anterior) {

    m.distancia += std::hypot((double)(atual.pos_x - anterior.pos_x), (double)(atual.pos_y - anterior.pos_y));
    m.ctx->definir_distancia_total(m.distancia);

    InstantaneoRobo s = m.ctx->instantaneo();

    verificar_defeito(m, atual, s);

    // Com defeito o robô não anda: a recarga fica suspensa até o defeito sumir
    if(!m.em_defeito)
        verificar_bateria(m, atual, s);
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
    d.fase = s.nav.fase;
    d.tarefa_id = s.nav.tarefa_id;
    d.progresso = s.nav.progresso;
    d.passagem = s.nav.passagem;
    d.aguardando_passagem = s.nav.aguardando_passagem;

    shm_slot_publicar(ctx->shm, ctx->slot, d);
}

void* thread_status(void* arg) {

    MonitorStatus m;
    m.ctx = (ContextoRobo*)arg;
    m.distancia = 0.0;
    m.em_defeito = false;
    m.recarga = RECARGA_NAO;
    m.vaga = -1;
    m.recarga_adiada = false;
    m.aviso_manual = false;
    snprintf(m.modulo, sizeof(m.modulo), "ROBO %d STATUS", m.ctx->id);

    Periodo periodo(PERIODO_STATUS_MS);

    LeituraHardware anterior = m.ctx->hw->ler();

    while(!m.ctx->deve_encerrar()) {

        LeituraHardware atual = m.ctx->hw->ler();

        avaliar_condicoes(m, atual, anterior);

        publicar(m.ctx, atual);

        anterior = atual;

        periodo.esperar();
    }

    recarga_liberar(m.ctx->shm, m.ctx->slot);

    log_msg(m.modulo, "encerrada: %lu ciclos, %lu atrasos, pior execução %ld us, distância %.1f",
            periodo.ciclos(), periodo.atrasos(), periodo.max_exec_us(), m.distancia);

    return NULL;
}
