#ifndef COMUM_IPC_ROBO_HPP
#define COMUM_IPC_ROBO_HPP

#include "comum/ipc_gestor.hpp"
#include "comum/mapa.hpp"

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
 *
 *  3. Controle das passagens estreitas do mapa e das vagas das estações de
 *     recarga, também em SHM_FROTA (comum/passagens.hpp, comum/recarga.hpp).
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

// Fases da tarefa de transporte executada pelo Comando de Navegação
enum FaseTarefa : int {
    FASE_NENHUMA = 0,
    FASE_IR_COLETA,             // indo até o ponto de coleta na baia
    FASE_AGUARDANDO_EQUIPE,     // na baia, esperando o Organizador liberar (ACOPLAR)
    FASE_ACOPLANDO,             // erguendo a carga; espera a equipe toda atracar
    FASE_TRANSPORTANDO,         // levando a carga junto com a equipe
    FASE_ENTREGANDO,            // no destino; solta quando a equipe toda chegar
    FASE_CONCLUIDA,
    FASE_FALHOU                 // ex.: não há rota para a equipe
};

const char* estado_robo_nome(int estado);

const char* modo_robo_nome(int modo);

const char* fase_tarefa_nome(int fase);


/* ------------------------------------------------------------------ */
/* Memória compartilhada: tabela de status                             */
/* ------------------------------------------------------------------ */

#define SHM_FROTA           "/atr_frota"
#define SHM_FROTA_MAGIC     0x46525441u     // "ATRF"
#define SHM_FROTA_VERSAO    3

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
    int fase;                   // FaseTarefa
    int tarefa_id;
    int progresso;              // waypoints da rota de transporte já alcançados
    int passagem;               // passagem estreita ocupada (-1 = nenhuma)
    int aguardando_passagem;    // passagem em cuja fila está (-1 = nenhuma)
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
    EV_RECARGA,
    EV_PASSAGEM,                // aguardando / entrou / saiu de passagem estreita
    EV_TAREFA                   // mudança de fase da tarefa de transporte
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


/* ------------------------------------------------------------------ */
/* Memória compartilhada: passagens estreitas (comum/passagens.hpp)    */
/* ------------------------------------------------------------------ */

#define MAX_RESERVAS        4

// Vagas guardadas para os membros de uma equipe que ainda vão entrar
struct ReservaPassagem {
    int equipe_id;              // tarefa_id da equipe (0 = reserva livre)
    uint32_t pendentes;         // slots que ainda vão entrar
};

struct PassagemShm {
    pthread_mutex_t mtx;        // PROCESS_SHARED + ROBUST
    int capacidade;
    uint32_t dentro;            // bit i = robô do slot i está na passagem
    ReservaPassagem reservas[MAX_RESERVAS];
    int n_fila;
    int fila[MAX_ROBOS];        // slots esperando para entrar (FIFO)
};


/* ------------------------------------------------------------------ */
/* Memória compartilhada: vagas de recarga (comum/recarga.hpp)         */
/* ------------------------------------------------------------------ */

struct RecargaShm {
    pthread_mutex_t mtx;        // PROCESS_SHARED + ROBUST
    int n_vagas;
    int ocupante[MAX_VAGAS_RECARGA];    // slot do robô que reservou, -1 se livre
};


struct ShmFrota {
    uint32_t magic;             // escrito por último na criação
    uint32_t versao;
    uint32_t tamanho;
    SlotStatus robos[MAX_ROBOS];
    BufferEventos eventos;
    int n_passagens;
    PassagemShm passagens[MAX_PASSAGENS];
    RecargaShm recarga;
};

// Cria (ou abre e valida, se já existir) a memória da frota. Usado pelo Gestor.
ShmFrota* shm_frota_criar(void);

// Abre a memória já criada. Usado pelos robôs, Organizador e Coletor.
ShmFrota* shm_frota_abrir(void);

void shm_frota_fechar(ShmFrota* shm);

// Trava um mutex robusto. Se o dono morreu com ele travado (EOWNERDEAD),
// marca como consistente e segue. Retorna 0 em sucesso, 1 se recuperou o
// mutex de um processo morto (o chamador pode querer reparar os dados).
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
    CMD_IR_PARA = 1,            // x, y: novo destino (cancela a tarefa atual)
    CMD_PARAR,                  // fica onde está
    CMD_MODO,                   // valor: ModoRobo
    CMD_ACOPLAR_E_ERGUER,       // o_acoplar_e_erguer = 1
    CMD_SOLTAR_CARGA,           // o_acoplar_e_erguer = 0
    CMD_TAREFA,                 // transporte: coleta (x, y) -> entrega (x2, y2),
                                // tarefa_id e equipe (máscara de slots)

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
    int x2;
    int y2;
    int valor;
    int tarefa_id;              // identifica a tarefa (> 0 em CMD_TAREFA)
    uint32_t equipe;            // CMD_TAREFA: bit i = robô do slot i na equipe (máx. 4)
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
