#ifndef COMUM_IPC_GESTOR_HPP
#define COMUM_IPC_GESTOR_HPP

#include <mqueue.h>
#include <sys/types.h>
#include <time.h>

/*
 * Canal assíncrono Gestor de Robôs -> Organizador da Logística.
 *
 * Fila de mensagens POSIX (mq_*): o Gestor publica um evento sempre que um
 * robô entra ou sai da frota e segue o seu trabalho sem esperar resposta.
 * O Organizador consome os eventos no seu próprio ritmo.
 */

#define MQ_GESTOR_ORGANIZADOR   "/atr_gestor_organizador"
#define MQ_GESTOR_MAX_MSGS      10      // limite padrão do Linux sem root (fs.mqueue.msg_max)

#define MAX_ROBOS               32      // tamanho máximo da frota

enum TipoEventoGestor : int {
    EVENTO_ROBO_NOVO     = 1,   // processo do robô criado
    EVENTO_ROBO_REMOVIDO = 2    // processo do robô terminou
};

struct EventoGestor {
    int tipo;                   // TipoEventoGestor
    int robo_id;
    pid_t pid;                  // processo que gerencia o robô
    int pos_x;                  // posição inicial (EVENTO_ROBO_NOVO)
    int pos_y;
    int status_saida;           // status do waitpid() (EVENTO_ROBO_REMOVIDO)
    struct timespec timestamp;  // CLOCK_REALTIME do evento
};

// Abre (criando se necessário) a fila para escrita. Retorna (mqd_t)-1 em erro.
mqd_t mq_gestor_abrir_escrita(void);

// Abre (criando se necessário) a fila para leitura bloqueante. Retorna (mqd_t)-1 em erro.
mqd_t mq_gestor_abrir_leitura(void);

// Envia um evento esperando no máximo timeout_ms se a fila estiver cheia.
// Retorna 0 em sucesso, -1 em erro/timeout.
int mq_gestor_enviar(mqd_t mq, const EventoGestor& evento, int timeout_ms);

// Bloqueia até receber um evento. Retorna 0 em sucesso, -1 em erro.
int mq_gestor_receber(mqd_t mq, EventoGestor& evento);

const char* evento_gestor_nome(int tipo);

#endif
