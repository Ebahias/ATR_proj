#ifndef COMUM_IPC_COLETOR_HPP
#define COMUM_IPC_COLETOR_HPP

#include "comum/ipc_robo.hpp"

#include <mqueue.h>
#include <time.h>

/*
 * Canal assíncrono Coletor de Dados -> Organizador da Logística.
 *
 * O Coletor grava tudo em disco e, em paralelo, publica cada registro nesta
 * fila de mensagens POSIX para que o Organizador os repasse à Tela de
 * Operação (via MQTT, na Etapa 2). O envio nunca bloqueia o Coletor: se o
 * Organizador não estiver lendo e a fila encher, o registro continua salvo em
 * disco e só a cópia em tempo real é perdida (contada nas estatísticas).
 */

#define MQ_COLETOR_ORGANIZADOR  "/atr_coletor_organizador"
#define MQ_COLETOR_MAX_MSGS     10      // limite padrão do Linux sem root (fs.mqueue.msg_max)

#define MAX_ROBOS_CARGA         8       // robôs listados em um registro de carga (equipe máx. 4)
#define TAM_NOME_LOCAL          24

enum TipoRegistroColetor : int {
    REG_EVENTO = 1,             // evento do histórico de um robô
    REG_CARGA                   // rastreabilidade: carga entregue (ou incompleta)
};

struct RegistroColetor {
    int tipo;                   // TipoRegistroColetor
    struct timespec timestamp;  // REG_EVENTO: hora do evento; REG_CARGA: hora da entrega

    // REG_EVENTO
    int robo_id;
    int tipo_evento;            // TipoEventoRobo
    char texto[TAM_TEXTO_EVENTO];

    // REG_CARGA
    int tarefa_id;
    char baia[TAM_NOME_LOCAL];
    char destino[TAM_NOME_LOCAL];
    int n_robos;
    int robos[MAX_ROBOS_CARGA]; // robôs que carregaram a carga
    struct timespec t_coleta;   // hora em que a equipe pegou a carga
    int completa;               // 1 = entregue por toda a equipe
};

// Abre (criando se necessário) a fila para escrita NÃO bloqueante. (mqd_t)-1 em erro.
mqd_t mq_coletor_abrir_escrita(void);

// Abre (criando se necessário) a fila para leitura bloqueante. (mqd_t)-1 em erro.
mqd_t mq_coletor_abrir_leitura(void);

// Envia sem bloquear. Retorna -1 (errno = EAGAIN) se a fila estiver cheia.
int mq_coletor_enviar(mqd_t mq, const RegistroColetor& registro);

// Bloqueia até receber um registro. Retorna 0 em sucesso, -1 em erro.
int mq_coletor_receber(mqd_t mq, RegistroColetor& registro);

#endif
