#include "comum/ipc_robo.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


const char* estado_robo_nome(int estado) {

    switch(estado) {
        case ESTADO_INATIVO:        return "inativo";
        case ESTADO_INICIANDO:      return "iniciando";
        case ESTADO_DISPONIVEL:     return "disponível";
        case ESTADO_DESLOCANDO:     return "deslocando";
        case ESTADO_OPERANDO_CARGA: return "operando carga";
        case ESTADO_RECARREGANDO:   return "recarregando";
        case ESTADO_DEFEITO:        return "defeito";
        default:                    return "?";
    }
}

const char* modo_robo_nome(int modo) {
    return modo == MODO_MANUAL ? "manual" : "automático";
}

const char* fase_tarefa_nome(int fase) {

    switch(fase) {
        case FASE_NENHUMA:           return "-";
        case FASE_IR_COLETA:         return "ir_coleta";
        case FASE_AGUARDANDO_EQUIPE: return "aguardando_equipe";
        case FASE_ACOPLANDO:         return "acoplando";
        case FASE_TRANSPORTANDO:     return "transportando";
        case FASE_ENTREGANDO:        return "entregando";
        case FASE_CONCLUIDA:         return "concluida";
        case FASE_FALHOU:            return "falhou";
        default:                     return "?";
    }
}

const char* tipo_evento_robo_nome(int tipo) {

    switch(tipo) {
        case EV_INICIADO:       return "INICIADO";
        case EV_ENCERRADO:      return "ENCERRADO";
        case EV_ESTADO:         return "ESTADO";
        case EV_MODO:           return "MODO";
        case EV_COMANDO:        return "COMANDO";
        case EV_CHEGOU_DESTINO: return "CHEGOU_DESTINO";
        case EV_CARGA:          return "CARGA";
        case EV_DEFEITO:        return "DEFEITO";
        case EV_RECARGA:        return "RECARGA";
        case EV_PASSAGEM:       return "PASSAGEM";
        case EV_TAREFA:         return "TAREFA";
        default:                return "?";
    }
}

const char* tipo_comando_nome(int tipo) {

    switch(tipo) {
        case CMD_IR_PARA:          return "IR_PARA";
        case CMD_PARAR:            return "PARAR";
        case CMD_MODO:             return "MODO";
        case CMD_ACOPLAR_E_ERGUER: return "ACOPLAR_E_ERGUER";
        case CMD_SOLTAR_CARGA:     return "SOLTAR_CARGA";
        case CMD_TAREFA:           return "TAREFA";
        case CMD_SIM_FALHA:        return "SIM_FALHA";
        case CMD_SIM_BATERIA:      return "SIM_BATERIA";
        default:                   return "?";
    }
}


/* ------------------------------------------------------------------ */
/* Memória compartilhada                                               */
/* ------------------------------------------------------------------ */

static void iniciar_mutex_compartilhado(pthread_mutex_t* mtx) {

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(mtx, &attr);
    pthread_mutexattr_destroy(&attr);
}

static ShmFrota* mapear(int fd) {

    void* p = mmap(NULL, sizeof(ShmFrota), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    close(fd);  // o mapeamento continua válido sem o descritor

    if(p == MAP_FAILED) {
        log_msg("IPC", "mmap(%s): %s", SHM_FROTA, strerror(errno));
        return NULL;
    }

    return (ShmFrota*)p;
}

// Espera o criador terminar a inicialização e confere a versão do layout.
static ShmFrota* validar(ShmFrota* shm) {

    for(int i=0;i<100 && __atomic_load_n(&shm->magic, __ATOMIC_ACQUIRE) != SHM_FROTA_MAGIC;i++)
        usleep(10000);

    if(shm->magic != SHM_FROTA_MAGIC || shm->versao != SHM_FROTA_VERSAO
       || shm->tamanho != sizeof(ShmFrota)) {
        log_msg("IPC", "%s com layout incompatível; remova /dev/shm%s", SHM_FROTA, SHM_FROTA);
        shm_frota_fechar(shm);
        return NULL;
    }

    return shm;
}

ShmFrota* shm_frota_criar(void) {

    int fd = shm_open(SHM_FROTA, O_RDWR | O_CREAT | O_EXCL, 0660);

    if(fd < 0 && errno == EEXIST) {
        // Já existe (outra execução ou outro processo central): só valida
        fd = shm_open(SHM_FROTA, O_RDWR, 0);

        struct stat st;
        if(fd >= 0 && fstat(fd, &st) == 0 && st.st_size != (off_t)sizeof(ShmFrota)) {
            log_msg("IPC", "%s com tamanho errado; remova /dev/shm%s", SHM_FROTA, SHM_FROTA);
            close(fd);
            return NULL;
        }

        if(fd < 0) {
            log_msg("IPC", "shm_open(%s): %s", SHM_FROTA, strerror(errno));
            return NULL;
        }

        ShmFrota* shm = mapear(fd);

        return shm ? validar(shm) : NULL;
    }

    if(fd < 0) {
        log_msg("IPC", "shm_open(%s): %s", SHM_FROTA, strerror(errno));
        return NULL;
    }

    if(ftruncate(fd, sizeof(ShmFrota)) < 0) {
        log_msg("IPC", "ftruncate(%s): %s", SHM_FROTA, strerror(errno));
        close(fd);
        shm_unlink(SHM_FROTA);
        return NULL;
    }

    ShmFrota* shm = mapear(fd);

    if(!shm) {
        shm_unlink(SHM_FROTA);
        return NULL;
    }

    // ftruncate zera a memória: só falta iniciar os mutexes e semáforos
    for(int i=0;i<MAX_ROBOS;i++)
        iniciar_mutex_compartilhado(&shm->robos[i].mtx);

    iniciar_mutex_compartilhado(&shm->eventos.mtx);
    sem_init(&shm->eventos.vazios, 1, TAM_BUFFER_EVENTOS);   // pshared = 1
    sem_init(&shm->eventos.cheios, 1, 0);

    shm->n_passagens = mapa_num_passagens();

    for(int i=0;i<shm->n_passagens;i++) {
        iniciar_mutex_compartilhado(&shm->passagens[i].mtx);
        shm->passagens[i].capacidade = mapa_passagem(i).capacidade;
    }

    iniciar_mutex_compartilhado(&shm->recarga.mtx);
    shm->recarga.n_vagas = mapa_num_vagas_recarga();

    for(int i=0;i<MAX_VAGAS_RECARGA;i++)
        shm->recarga.ocupante[i] = -1;

    shm->versao = SHM_FROTA_VERSAO;
    shm->tamanho = sizeof(ShmFrota);

    // Por último: quem abrir a memória só a usa depois de ver o magic
    __atomic_store_n(&shm->magic, SHM_FROTA_MAGIC, __ATOMIC_RELEASE);

    return shm;
}

ShmFrota* shm_frota_abrir(void) {

    int fd = shm_open(SHM_FROTA, O_RDWR, 0);

    if(fd < 0) {
        log_msg("IPC", "shm_open(%s): %s (o Gestor está rodando?)", SHM_FROTA, strerror(errno));
        return NULL;
    }

    ShmFrota* shm = mapear(fd);

    return shm ? validar(shm) : NULL;
}

void shm_frota_fechar(ShmFrota* shm) {

    if(shm)
        munmap(shm, sizeof(ShmFrota));
}

int shm_lock(pthread_mutex_t* mtx) {

    int ret = pthread_mutex_lock(mtx);

    if(ret == EOWNERDEAD) {
        // Um processo morreu segurando o mutex: recupera. Quem protege dados
        // que podem ter ficado pela metade recebe 1 e repara.
        pthread_mutex_consistent(mtx);
        ret = 1;
    }

    return ret;
}

void shm_unlock(pthread_mutex_t* mtx) {
    pthread_mutex_unlock(mtx);
}

void shm_slot_ocupar(ShmFrota* shm, int slot, int robo_id, pid_t pid, int x, int y) {

    SlotStatus& s = shm->robos[slot];

    shm_lock(&s.mtx);

    memset(&s.d, 0, sizeof(s.d));
    s.d.ativo = 1;
    s.d.robo_id = robo_id;
    s.d.pid = pid;
    s.d.estado = ESTADO_INICIANDO;
    s.d.modo = MODO_AUTOMATICO;
    s.d.pos_x = s.d.sp_x = x;
    s.d.pos_y = s.d.sp_y = y;
    s.d.bateria = 100;
    s.d.passagem = -1;
    s.d.aguardando_passagem = -1;
    clock_gettime(CLOCK_REALTIME, &s.d.atualizado_em);

    shm_unlock(&s.mtx);
}

void shm_slot_definir_pid(ShmFrota* shm, int slot, pid_t pid) {

    SlotStatus& s = shm->robos[slot];

    shm_lock(&s.mtx);
    s.d.pid = pid;
    shm_unlock(&s.mtx);
}

void shm_slot_liberar(ShmFrota* shm, int slot) {

    SlotStatus& s = shm->robos[slot];

    shm_lock(&s.mtx);

    s.d.ativo = 0;
    s.d.estado = ESTADO_INATIVO;
    s.d.seq++;
    clock_gettime(CLOCK_REALTIME, &s.d.atualizado_em);

    shm_unlock(&s.mtx);
}

void shm_slot_publicar(ShmFrota* shm, int slot, const DadosStatus& dados) {

    SlotStatus& s = shm->robos[slot];

    shm_lock(&s.mtx);

    // Identidade do slot é do Gestor; o robô não sobrescreve
    int ativo = s.d.ativo;
    int robo_id = s.d.robo_id;
    pid_t pid = s.d.pid;
    uint32_t seq = s.d.seq;

    s.d = dados;
    s.d.ativo = ativo;
    s.d.robo_id = robo_id;
    s.d.pid = pid;
    s.d.seq = seq + 1;
    clock_gettime(CLOCK_REALTIME, &s.d.atualizado_em);

    shm_unlock(&s.mtx);
}

void shm_slot_ler(ShmFrota* shm, int slot, DadosStatus& dados) {

    SlotStatus& s = shm->robos[slot];

    shm_lock(&s.mtx);
    dados = s.d;
    shm_unlock(&s.mtx);
}

int shm_evento_publicar(ShmFrota* shm, int robo_id, int tipo, const char* fmt, ...) {

    BufferEventos& b = shm->eventos;

    // Produtor de tempo real não espera o Coletor: se não há vaga, descarta
    if(sem_trywait(&b.vazios) < 0) {
        __atomic_add_fetch(&b.descartados, 1, __ATOMIC_RELAXED);
        return -1;
    }

    EventoRobo ev;
    ev.robo_id = robo_id;
    ev.tipo = tipo;
    clock_gettime(CLOCK_REALTIME, &ev.timestamp);

    va_list args;
    va_start(args, fmt);
    vsnprintf(ev.texto, sizeof(ev.texto), fmt, args);
    va_end(args);

    shm_lock(&b.mtx);
    b.ev[b.fim] = ev;
    b.fim = (b.fim + 1) % TAM_BUFFER_EVENTOS;
    shm_unlock(&b.mtx);

    sem_post(&b.cheios);

    return 0;
}

int shm_evento_consumir(ShmFrota* shm, EventoRobo& evento, int timeout_ms) {

    BufferEventos& b = shm->eventos;
    int ret;

    if(timeout_ms < 0) {
        do { ret = sem_wait(&b.cheios); } while(ret < 0 && errno == EINTR);
    } else {
        struct timespec prazo;
        clock_gettime(CLOCK_REALTIME, &prazo);
        prazo.tv_sec += timeout_ms / 1000;
        prazo.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if(prazo.tv_nsec >= 1000000000L) {
            prazo.tv_sec++;
            prazo.tv_nsec -= 1000000000L;
        }
        do { ret = sem_timedwait(&b.cheios, &prazo); } while(ret < 0 && errno == EINTR);
    }

    if(ret < 0)
        return -1;

    shm_lock(&b.mtx);
    evento = b.ev[b.inicio];
    b.inicio = (b.inicio + 1) % TAM_BUFFER_EVENTOS;
    shm_unlock(&b.mtx);

    sem_post(&b.vazios);

    return 0;
}


/* ------------------------------------------------------------------ */
/* Fila de comandos                                                    */
/* ------------------------------------------------------------------ */

void nome_fila_comando(int robo_id, char* nome, size_t tam) {
    snprintf(nome, tam, "/atr_robo_%d_cmd", robo_id);
}

static mqd_t abrir_comando(int robo_id, int flags) {

    char nome[64];
    nome_fila_comando(robo_id, nome, sizeof(nome));

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = MQ_COMANDO_MAX_MSGS;
    attr.mq_msgsize = sizeof(ComandoRobo);

    mqd_t mq = mq_open(nome, flags, 0660, &attr);

    if(mq == (mqd_t)-1)
        log_msg("IPC", "mq_open(%s): %s", nome, strerror(errno));

    return mq;
}

int mq_comando_criar(int robo_id) {

    // Remove uma fila de mesmo nome deixada por outra execução (com comandos velhos)
    mq_comando_remover(robo_id);

    mqd_t mq = abrir_comando(robo_id, O_RDWR | O_CREAT | O_EXCL);

    if(mq == (mqd_t)-1)
        return -1;

    mq_close(mq);   // a fila persiste até mq_unlink

    return 0;
}

void mq_comando_remover(int robo_id) {

    char nome[64];
    nome_fila_comando(robo_id, nome, sizeof(nome));

    mq_unlink(nome);
}

mqd_t mq_comando_abrir_leitura(int robo_id) {
    return abrir_comando(robo_id, O_RDONLY | O_NONBLOCK);
}

mqd_t mq_comando_abrir_escrita(int robo_id) {
    return abrir_comando(robo_id, O_WRONLY | O_NONBLOCK);
}

int mq_comando_enviar(mqd_t mq, const ComandoRobo& cmd) {

    ComandoRobo c = cmd;

    if(c.timestamp.tv_sec == 0)
        clock_gettime(CLOCK_REALTIME, &c.timestamp);

    return mq_send(mq, (const char*)&c, sizeof(c), 0);
}
