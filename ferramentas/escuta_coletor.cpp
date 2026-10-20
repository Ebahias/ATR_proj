/*
 * Lê e imprime os registros que o Coletor de Dados envia ao Organizador da
 * Logística. Serve para testar o canal enquanto o Organizador não existe, e de
 * exemplo de como o Organizador deve consumir a fila (e repassar ao MQTT).
 */

#include "comum/ipc_coletor.hpp"
#include "comum/log.hpp"

#include <string>

static std::string hora(const struct timespec& ts) {

    if(ts.tv_sec == 0 && ts.tv_nsec == 0)
        return "-";

    char buf[48];
    struct tm t;
    localtime_r(&ts.tv_sec, &t);

    size_t n = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    snprintf(buf + n, sizeof(buf) - n, ".%03ld", ts.tv_nsec / 1000000);

    return buf;
}

int main(void) {

    mqd_t mq = mq_coletor_abrir_leitura();

    if(mq == (mqd_t)-1)
        return 1;

    log_msg("ESCUTA", "aguardando registros em %s", MQ_COLETOR_ORGANIZADOR);

    RegistroColetor r;

    while(mq_coletor_receber(mq, r) == 0) {

        if(r.tipo == REG_EVENTO) {

            log_msg("ESCUTA", "EVENTO robo=%d %s: %s [%s]", r.robo_id,
                    tipo_evento_robo_nome(r.tipo_evento), r.texto, hora(r.timestamp).c_str());

        } else {

            std::string robos;

            for(int i=0;i<r.n_robos;i++)
                robos += (robos.empty() ? "" : "+") + std::to_string(r.robos[i]);

            log_msg("ESCUTA", "CARGA tarefa=%d baia=%s destino=%s robos=%s completa=%d coleta=[%s] entrega=[%s]",
                    r.tarefa_id, r.baia, r.destino, robos.c_str(), r.completa,
                    hora(r.t_coleta).c_str(), hora(r.timestamp).c_str());
        }
    }

    mq_close(mq);

    return 0;
}
