#include "comum/ipc_coletor.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>

static mqd_t abrir(int flags) {

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = MQ_COLETOR_MAX_MSGS;
    attr.mq_msgsize = sizeof(RegistroColetor);

    mqd_t mq = mq_open(MQ_COLETOR_ORGANIZADOR, flags | O_CREAT, 0660, &attr);

    if(mq == (mqd_t)-1) {
        log_msg("IPC", "mq_open(%s): %s", MQ_COLETOR_ORGANIZADOR, strerror(errno));
        return mq;
    }

    // Se a fila já existia (outra versão do programa), os atributos acima são ignorados
    mq_getattr(mq, &attr);

    if(attr.mq_msgsize != (long)sizeof(RegistroColetor)) {
        log_msg("IPC", "fila %s com msgsize=%ld (esperado %zu); remova /dev/mqueue%s",
                MQ_COLETOR_ORGANIZADOR, attr.mq_msgsize, sizeof(RegistroColetor), MQ_COLETOR_ORGANIZADOR);
        mq_close(mq);
        return (mqd_t)-1;
    }

    return mq;
}

mqd_t mq_coletor_abrir_escrita(void) {
    return abrir(O_WRONLY | O_NONBLOCK);
}

mqd_t mq_coletor_abrir_leitura(void) {
    return abrir(O_RDONLY);
}

int mq_coletor_enviar(mqd_t mq, const RegistroColetor& registro) {

    int ret;

    do {
        ret = mq_send(mq, (const char*)&registro, sizeof(registro), 0);
    } while(ret < 0 && errno == EINTR);

    return ret;
}

int mq_coletor_receber(mqd_t mq, RegistroColetor& registro) {

    ssize_t n;

    do {
        n = mq_receive(mq, (char*)&registro, sizeof(registro), NULL);
    } while(n < 0 && errno == EINTR);

    return (n == (ssize_t)sizeof(registro)) ? 0 : -1;
}
