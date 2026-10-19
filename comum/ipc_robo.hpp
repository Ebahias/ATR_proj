#ifndef COMUM_IPC_ROBO_HPP
#define COMUM_IPC_ROBO_HPP

#include "comum/ipc_gestor.hpp"

#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/*
 * Contrato de IPC entre os processos dos robôs e o programa central
 * (Gestor de Robôs, Organizador da Logística e Coletor de Dados).
 *
 *  1. Memória compartilhada POSIX SHM_FROTA, criada pelo Gestor:
 *     - tabela de status: uma posição (slot) por robô, cada uma com seu
 *       próprio mutex robusto entre processos. Escrita pela thread de Status
 *       do robô e lida pelo Organizador ("último valor vale");
 *     - buffer circular de eventos (produtor/consumidor com semáforos):
 *       os robôs produzem, o Coletor de Dados consome.
 *
 *  2. Uma fila de mensagens POSIX de comandos por robô ("/atr_robo_<id>_cmd"),
 *     criada pelo Gestor antes do fork e removida quando o robô termina.
 *     O Organizador (ou o operador, via Organizador) envia; a thread de
 *     Comando de Navegação do robô consome.
 */


/* ------------------------------------------------------------------ */
/* Estados (Tabela 2 do enunciado)                                     */
/* ------------------------------------------------------------------ */

enum EstadoRobo : int {
    ESTADO_INATIVO = 0,         // slot livre
    ESTADO_INICIANDO,           // processo criado, threads ainda não rodando
    ESTADO_DISPONIVEL,
    ESTADO_DESLOCANDO,
    ESTADO_OPERANDO_CARGA,
    ESTADO_RECARREGANDO,
    ESTADO_DEFEITO
};

enum ModoRobo : int {
    MODO_AUTOMATICO = 0,        // setpoints vêm do Organizador
    MODO_MANUAL                 // setpoints vêm do operador
};

const char* estado_robo_nome(int estado);

const char* modo_robo_nome(int modo);


/* ------------------------------------------------------------------ */
/* Memória compartilhada: tabela de status                             */
/* ------------------------------------------------------------------ */

#define SHM_FROTA           "/atr_frota"
#define SHM_FROTA_MAGIC     0x46525441u     // "ATRF"
#define SHM_FROTA_VERSAO    1

struct DadosStatus {
    int ativo;
    int robo_id;
    pid_t pid;
    int estado;                 // EstadoRobo
    int modo;                   // ModoRobo
    int pos_x;                  // e_posicao
    int pos_y;
    int sp_x;                   // j_sp_posicao
    int sp_y;
    int bateria;                // 0 a 100 %
    int carga_atracada;
    int falha;
    double distancia_total;
    uint32_t seq;               // incrementado a cada publicação
    struct timespec atualizado_em;
};

struct SlotStatus {
    pthread_mutex_t mtx;        // PROCESS_SHARED + ROBUST
    DadosStatus d;
};


/* ------------------------------------------------------------------ */
/* Memória compartilhada: buffer de eventos (para o Coletor de Dados)  */
/* ------------------------------------------------------------------ */

#define TAM_BUFFER_EVENTOS  64
#define TAM_TEXTO_EVENTO    96

enum TipoEventoRobo : int {
    EV_INICIADO = 1,
    EV_ENCERRADO,
    EV_ESTADO,                  // mudança de estado
    EV_MODO,                    // troca manual/automático
    EV_COMANDO,                 // comando recebido
    EV_CHEGOU_DESTINO,
    EV_CARGA,                   // acoplou / soltou carga
    EV_DEFEITO,
    EV_RECARGA
};

struct EventoRobo {
    int robo_id;
    int tipo;                   // TipoEventoRobo
    char texto[TAM_TEXTO_EVENTO];
    struct timespec timestamp;  // CLOCK_REALTIME
};

struct BufferEventos {
    pthread_mutex_t mtx;        // PROCESS_SHARED + ROBUST
    sem_t vazios;               // posições livres
    sem_t cheios;               // eventos disponíveis
    int inicio;
    int fim;
    uint32_t descartados;       // eventos perdidos por buffer cheio
    EventoRobo ev[TAM_BUFFER_EVENTOS];
};

const char* tipo_evento_robo_nome(int tipo);


struct ShmFrota {
    uint32_t magic;             // escrito por último na criação
    uint32_t versao;
    uint32_t tamanho;
    SlotStatus robos[MAX_ROBOS];
    BufferEventos eventos;
};

// Cria (ou abre e valida, se já existir) a memória da frota. Usado pelo Gestor.
ShmFrota* shm_frota_criar(void);

// Abre a memória já criada. Usado pelos robôs, Organizador e Coletor.
ShmFrota* shm_frota_abrir(void);

void shm_frota_fechar(ShmFrota* shm);

// Trava um mutex robusto. Se o dono morreu com ele travado (EOWNERDEAD),
// marca como consistente e segue. Retorna 0 em sucesso.
int shm_lock(pthread_mutex_t* mtx);

void shm_unlock(pthread_mutex_t* mtx);

// Gestor: reserva/libera o slot de um robô.
void shm_slot_ocupar(ShmFrota* shm, int slot, int robo_id, pid_t pid, int x, int y);

void shm_slot_definir_pid(ShmFrota* shm, int slot, pid_t pid);

void shm_slot_liberar(ShmFrota* shm, int slot);

// Robô: publica o status. A identidade do slot (ativo, robo_id, pid) é do
// Gestor e é preservada; seq e atualizado_em são preenchidos aqui.
void shm_slot_publicar(ShmFrota* shm, int slot, const DadosStatus& dados);

// Organizador / telas: cópia consistente do status de um slot.
void shm_slot_ler(ShmFrota* shm, int slot, DadosStatus& dados);

// Produtor: nunca bloqueia (tempo real). Retorna -1 e conta como descartado
// se o buffer estiver cheio.
int shm_evento_publicar(ShmFrota* shm, int robo_id, int tipo, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Consumidor: espera até timeout_ms (-1 = indefinidamente).
// Retorna 0 com evento, -1 em timeout/erro.
int shm_evento_consumir(ShmFrota* shm, EventoRobo& evento, int timeout_ms);


/* ------------------------------------------------------------------ */
/* Fila de comandos de cada robô                                       */
/* ------------------------------------------------------------------ */

#define MQ_COMANDO_MAX_MSGS 10

enum TipoComando : int {
    CMD_IR_PARA = 1,            // x, y: novo destino
    CMD_PARAR,                  // fica onde está
    CMD_MODO,                   // valor: ModoRobo
    CMD_ACOPLAR_E_ERGUER,       // o_acoplar_e_erguer = 1
    CMD_SOLTAR_CARGA,           // o_acoplar_e_erguer = 0

    // Só na Etapa 1: fazem o papel da interface do simulador
    CMD_SIM_FALHA = 100,        // valor: 0/1
    CMD_SIM_BATERIA             // valor: percentual
};

enum OrigemComando : int {
    ORIGEM_ORGANIZADOR = 0,
    ORIGEM_OPERADOR
};

struct ComandoRobo {
    int tipo;                   // TipoComando
    int origem;                 // OrigemComando
    int x;
    int y;
    int valor;
    int tarefa_id;              // livre para o Organizador identificar a tarefa
    struct timespec timestamp;
};

const char* tipo_comando_nome(int tipo);

void nome_fila_comando(int robo_id, char* nome, size_t tam);

// Gestor: cria a fila vazia (remove uma antiga de mesmo nome) / remove a fila.
int mq_comando_criar(int robo_id);

void mq_comando_remover(int robo_id);

// Robô: abre para leitura não bloqueante (a navegação esvazia a fila a cada ciclo).
mqd_t mq_comando_abrir_leitura(int robo_id);

// Organizador: abre para escrita não bloqueante.
mqd_t mq_comando_abrir_escrita(int robo_id);

// Envia sem bloquear. Retorna -1 com errno = EAGAIN se a fila estiver cheia.
int mq_comando_enviar(mqd_t mq, const ComandoRobo& cmd);

#endif
