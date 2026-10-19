/*
 * Processo de um robô (tarefa 2). Criado pelo Gestor de Robôs como:
 *
 *     robo <id> <slot> <x> <y>
 *
 *   id    identificador do robô (anunciado pelo hardware)
 *   slot  posição do robô na tabela de status da memória compartilhada
 *   x, y  posição inicial
 *
 * Threads:
 *   - principal:  trata SIGINT/SIGTERM (sigwait) e coordena o encerramento
 *   - navegação:  Comando de Navegação (robo/navegacao.cpp)
 *   - status:     Status do Robô (robo/status.cpp)
 *   - física:     hardware simulado (só na Etapa 1, robo/hardware_robo.cpp)
 *
 * IPC: lê comandos da fila "/atr_robo_<id>_cmd" e publica status e eventos
 * na memória compartilhada SHM_FROTA (comum/ipc_robo.hpp).
 */

#include "robo/contexto_robo.hpp"
#include "robo/hardware_robo.hpp"
#include "robo/threads_robo.hpp"
#include "comum/ipc_robo.hpp"
#include "comum/log.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static bool ler_int(const char* s, int& valor) {

    char* fim;
    long v = strtol(s, &fim, 10);

    if(*s == '\0' || *fim != '\0')
        return false;

    valor = (int)v;

    return true;
}

int main(int argc, char** argv) {

    int id, slot, x, y;

    if(argc != 5 || !ler_int(argv[1], id) || !ler_int(argv[2], slot)
       || !ler_int(argv[3], x) || !ler_int(argv[4], y)
       || id <= 0 || slot < 0 || slot >= MAX_ROBOS) {
        fprintf(stderr, "uso: %s <id> <slot 0..%d> <x> <y>\n", argv[0], MAX_ROBOS - 1);
        return 2;
    }

    char modulo[16];
    snprintf(modulo, sizeof(modulo), "ROBO %d", id);

    // Bloqueado antes de criar threads: só a principal trata, com sigwait()
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    ShmFrota* shm = shm_frota_abrir();

    if(!shm)
        return 1;

    mqd_t mq = mq_comando_abrir_leitura(id);

    if(mq == (mqd_t)-1) {
        shm_frota_fechar(shm);
        return 1;
    }

    HardwareSimulado hw(x, y);

    if(!hw.iniciar()) {
        log_msg(modulo, "falha ao iniciar o hardware");
        mq_close(mq);
        shm_frota_fechar(shm);
        return 1;
    }

    ContextoRobo ctx(id, slot, shm, &hw, mq);

    pthread_t th_nav, th_status;

    if(pthread_create(&th_status, NULL, thread_status, &ctx) != 0) {
        log_msg(modulo, "falha ao criar a thread de status");
        return 1;
    }

    if(pthread_create(&th_nav, NULL, thread_navegacao, &ctx) != 0) {
        log_msg(modulo, "falha ao criar a thread de navegação");
        ctx.solicitar_encerramento();
        pthread_join(th_status, NULL);
        return 1;
    }

    ctx.mudar_estado(ESTADO_DISPONIVEL, "threads iniciadas");
    shm_evento_publicar(shm, id, EV_INICIADO, "processo %d iniciado em (%d, %d)", getpid(), x, y);
    log_msg(modulo, "processo iniciado (pid %d, slot %d) em (%d, %d)", getpid(), slot, x, y);

    int sinal;
    sigwait(&sinais, &sinal);

    log_msg(modulo, "encerrando (%s)", strsignal(sinal));

    ctx.solicitar_encerramento();
    pthread_join(th_nav, NULL);
    pthread_join(th_status, NULL);
    hw.parar();

    shm_evento_publicar(shm, id, EV_ENCERRADO, "processo %d encerrado", getpid());

    mq_close(mq);
    shm_frota_fechar(shm);

    return 0;
}
