#include "comum/ipc_gestor.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>

static mqd_t abrir(int flags) {

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = MQ_GESTOR_MAX_MSGS;
    attr.mq_msgsize = sizeof(EventoGestor);

    mqd_t mq = mq_open(MQ_GESTOR_ORGANIZADOR, flags | O_CREAT, 0660, &attr);

    if(mq == (mqd_t)-1) {
        log_msg("IPC", "mq_open(%s): %s", MQ_GESTOR_ORGANIZADOR, strerror(errno));
        return mq;
    }

    // Se a fila já existia (criada por outra versão do programa), os atributos
    // passados acima são ignorados. Garante que o tamanho da mensagem bate.
    mq_getattr(mq, &attr);

    if(attr.mq_msgsize != (long)sizeof(EventoGestor)) {
        log_msg("IPC", "fila %s com msgsize=%ld (esperado %zu); remova /dev/mqueue%s",
                MQ_GESTOR_ORGANIZADOR, attr.mq_msgsize, sizeof(EventoGestor), MQ_GESTOR_ORGANIZADOR);
        mq_close(mq);
        return (mqd_t)-1;
    }

    return mq;
}

mqd_t mq_gestor_abrir_escrita(void) {
    return abrir(O_WRONLY);
}

mqd_t mq_gestor_abrir_leitura(void) {
    return abrir(O_RDONLY);
}

int mq_gestor_enviar(mqd_t mq, const EventoGestor& evento, int timeout_ms) {

    struct timespec limite;
    clock_gettime(CLOCK_REALTIME, &limite);

    limite.tv_sec += timeout_ms / 1000;
    limite.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;

    if(limite.tv_nsec >= 1000000000L) {
        limite.tv_sec++;
        limite.tv_nsec -= 1000000000L;
    }

    while(mq_timedsend(mq, (const char*)&evento, sizeof(evento), 0, &limite) < 0) {
        if(errno != EINTR)
            return -1;
    }

    return 0;
}

int mq_gestor_receber(mqd_t mq, EventoGestor& evento) {

    ssize_t n;

    do {
        n = mq_receive(mq, (char*)&evento, sizeof(evento), NULL);
    } while(n < 0 && errno == EINTR);

    return (n == (ssize_t)sizeof(evento)) ? 0 : -1;
}

const char* evento_gestor_nome(int tipo) {

    switch(tipo) {
        case EVENTO_ROBO_NOVO:     return "ROBO_NOVO";
        case EVENTO_ROBO_REMOVIDO: return "ROBO_REMOVIDO";
        default:                   return "DESCONHECIDO";
    }
}
