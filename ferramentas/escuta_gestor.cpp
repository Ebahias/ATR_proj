/*
 * Lê e imprime os eventos que o Gestor de Robôs envia ao Organizador da
 * Logística. Serve para testar a comunicação enquanto o Organizador não
 * existe, e de exemplo de como o Organizador deve consumir a fila.
 */

#include "comum/ipc_gestor.hpp"
#include "comum/log.hpp"

#include <sys/wait.h>

int main(void) {

    mqd_t mq = mq_gestor_abrir_leitura();

    if(mq == (mqd_t)-1)
        return 1;

    log_msg("ESCUTA", "aguardando eventos em %s", MQ_GESTOR_ORGANIZADOR);

    EventoGestor ev;

    while(mq_gestor_receber(mq, ev) == 0) {

        if(ev.tipo == EVENTO_ROBO_NOVO) {
            log_msg("ESCUTA", "%s robo=%d pid=%d pos=(%d, %d) t=%ld.%09ld",
                    evento_gestor_nome(ev.tipo), ev.robo_id, ev.pid, ev.pos_x, ev.pos_y,
                    (long)ev.timestamp.tv_sec, ev.timestamp.tv_nsec);
        } else {
            log_msg("ESCUTA", "%s robo=%d pid=%d %s=%d t=%ld.%09ld",
                    evento_gestor_nome(ev.tipo), ev.robo_id, ev.pid,
                    WIFSIGNALED(ev.status_saida) ? "sinal" : "codigo",
                    WIFSIGNALED(ev.status_saida) ? WTERMSIG(ev.status_saida) : WEXITSTATUS(ev.status_saida),
                    (long)ev.timestamp.tv_sec, ev.timestamp.tv_nsec);
        }
    }

    mq_close(mq);

    return 0;
}
