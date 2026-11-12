#include "despacho.hpp"
#include "comum/log.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <time.h>

// Intervalo de leitura da tabela em despacho_esperar_fase. A thread de Status
// do robô publica a cada 100 ms; ler mais rápido que isso não adianta.
#define PERIODO_ESPERA_FASE_MS  100

static ShmFrota* g_shm = NULL;

static std::atomic<bool> g_interrompido(false);

bool despacho_iniciar(void) {

    if(g_shm != NULL)
        return true;

    g_shm = shm_frota_abrir();

    if(g_shm == NULL) {
        log_msg("DESPACHO", "não foi possível abrir %s: inicie o Gestor de Robôs antes", SHM_FROTA);
        return false;
    }

    return true;
}

void despacho_encerrar(void) {

    if(g_shm != NULL) {
        shm_frota_fechar(g_shm);
        g_shm = NULL;
    }
}

// Procura o robô e copia o status na mesma passada: ler o slot de novo depois
// abriria uma janela em que o Gestor poderia ter liberado ou reusado o slot.
static int buscar_robo(int robo_id, DadosStatus& d) {

    if(g_shm == NULL)
        return -1;

    for(int s=0;s<MAX_ROBOS;s++) {

        shm_slot_ler(g_shm, s, d);

        if(d.ativo && d.robo_id == robo_id)
            return s;
    }

    return -1;
}

int despacho_slot_do_robo(int robo_id) {

    DadosStatus d;

    return buscar_robo(robo_id, d);
}

bool despacho_ler_status(int robo_id, DadosStatus& d) {

    return buscar_robo(robo_id, d) >= 0;
}

int despacho_ler_frota(std::vector<DadosStatus>& ativos) {

    ativos.clear();

    if(g_shm == NULL)
        return 0;

    for(int s=0;s<MAX_ROBOS;s++) {

        DadosStatus d;
        shm_slot_ler(g_shm, s, d);

        if(d.ativo)
            ativos.push_back(d);
    }

    return (int)ativos.size();
}

uint32_t despacho_mascara_equipe(const std::vector<int>& ids) {

    uint32_t mascara = 0;

    for(int id : ids) {

        int slot = despacho_slot_do_robo(id);

        if(slot < 0) {
            log_msg("DESPACHO", "robô %d da equipe não está ativo", id);
            return 0;
        }

        mascara |= 1u << slot;
    }

    return mascara;
}

bool despacho_enviar(int robo_id, const ComandoRobo& cmd, int origem) {

    ComandoRobo c = cmd;
    c.origem = origem;
    clock_gettime(CLOCK_REALTIME, &c.timestamp);

    // Abre e fecha a cada envio: a fila é do robô e some quando ele morre
    // (o Gestor a remove). Um descritor guardado apontaria para uma fila
    // removida, e o envio "daria certo" sem ninguém para ler.
    mqd_t mq = mq_comando_abrir_escrita(robo_id);

    if(mq == (mqd_t)-1) {
        log_msg("DESPACHO", "%s para o robô %d: fila de comandos não encontrada (%s)",
                tipo_comando_nome(c.tipo), robo_id, strerror(errno));
        return false;
    }

    int ret = mq_comando_enviar(mq, c);
    int erro = errno;

    mq_close(mq);

    if(ret < 0) {
        log_msg("DESPACHO", "%s para o robô %d falhou: %s", tipo_comando_nome(c.tipo), robo_id,
                erro == EAGAIN ? "fila cheia (o robô não está consumindo)" : strerror(erro));
        return false;
    }

    return true;
}

bool despacho_tarefa(int robo_id, int tarefa_id, uint32_t equipe, int cx, int cy, int ex, int ey) {

    // O robô recusa CMD_TAREFA com tarefa_id <= 0; falhar aqui deixa o motivo
    // no log do Organizador, em vez de a missão só esperar até o timeout.
    if(tarefa_id <= 0 || equipe == 0) {
        log_msg("DESPACHO", "tarefa inválida para o robô %d (tarefa %d, equipe 0x%x)", robo_id, tarefa_id, equipe);
        return false;
    }

    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.tipo = CMD_TAREFA;
    cmd.tarefa_id = tarefa_id;
    cmd.equipe = equipe;
    cmd.x = cx;
    cmd.y = cy;
    cmd.x2 = ex;
    cmd.y2 = ey;

    if(!despacho_enviar(robo_id, cmd))
        return false;

    log_msg("DESPACHO", "TAREFA %d -> robô %d: coleta (%d, %d), entrega (%d, %d), equipe de %d",
            tarefa_id, robo_id, cx, cy, ex, ey, __builtin_popcount(equipe));

    return true;
}

bool despacho_acoplar(int robo_id) {

    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.tipo = CMD_ACOPLAR_E_ERGUER;

    if(!despacho_enviar(robo_id, cmd))
        return false;

    log_msg("DESPACHO", "ACOPLAR -> robô %d", robo_id);

    return true;
}

static bool enviar_simples(int robo_id, int tipo) {

    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.tipo = tipo;

    if(!despacho_enviar(robo_id, cmd))
        return false;

    log_msg("DESPACHO", "%s -> robô %d", tipo_comando_nome(tipo), robo_id);

    return true;
}

bool despacho_parar(int robo_id) {

    return enviar_simples(robo_id, CMD_PARAR);
}

bool despacho_soltar(int robo_id) {

    return enviar_simples(robo_id, CMD_SOLTAR_CARGA);
}

void despacho_interromper(void) {

    g_interrompido = true;
}

static long diff_ms(const struct timespec& a, const struct timespec& b) {

    return (b.tv_sec - a.tv_sec) * 1000L + (b.tv_nsec - a.tv_nsec) / 1000000L;
}

bool despacho_esperar_fase(const std::vector<int>& ids, int tarefa_id, int fase, int timeout_ms) {

    struct timespec inicio, agora;
    clock_gettime(CLOCK_MONOTONIC, &inicio);

    struct timespec intervalo = { 0, PERIODO_ESPERA_FASE_MS * 1000000L };

    // Robôs que já apareceram na tabela com esta tarefa: se um deles depois
    // aparece sem ela, a tarefa foi cancelada no robô
    std::vector<bool> visto(ids.size(), false);

    while(true) {

        if(g_interrompido) {
            log_msg("DESPACHO", "tarefa %d: espera interrompida (Organizador encerrando)", tarefa_id);
            return false;
        }

        int prontos = 0;

        for(size_t i=0;i<ids.size();i++) {

            int id = ids[i];

            DadosStatus d;

            if(!despacho_ler_status(id, d)) {
                log_msg("DESPACHO", "tarefa %d: robô %d não está mais ativo", tarefa_id, id);
                return false;
            }

            // Em manual o robô ignora o Organizador (e a troca de modo já
            // cancelou a tarefa nele): não adianta esperar o timeout
            if(d.modo == MODO_MANUAL) {
                log_msg("DESPACHO", "tarefa %d: robô %d passou para o modo manual", tarefa_id, id);
                return false;
            }

            // Logo depois do CMD_TAREFA a tabela ainda pode mostrar a tarefa
            // anterior do robô (inclusive FASE_FALHOU dela). Só a fase desta
            // tarefa conta, para sucesso ou para falha.
            if(d.tarefa_id != tarefa_id) {
                if(visto[i]) {
                    log_msg("DESPACHO", "tarefa %d: cancelada no robô %d (fase %s)", tarefa_id, id, fase_tarefa_nome(d.fase));
                    return false;
                }
                continue;
            }

            visto[i] = true;

            if(d.fase == FASE_FALHOU) {
                log_msg("DESPACHO", "tarefa %d: robô %d falhou (estado %s)", tarefa_id, id, estado_robo_nome(d.estado));
                return false;
            }

            if(d.fase == fase)
                prontos++;
        }

        if(prontos == (int)ids.size())
            return true;

        clock_gettime(CLOCK_MONOTONIC, &agora);

        if(diff_ms(inicio, agora) >= timeout_ms) {
            log_msg("DESPACHO", "tarefa %d: timeout de %d ms esperando a fase %s (%d de %d robôs prontos)",
                    tarefa_id, timeout_ms, fase_tarefa_nome(fase), prontos, (int)ids.size());
            return false;
        }

        nanosleep(&intervalo, NULL);
    }
}
