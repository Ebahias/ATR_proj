#include "coletor/coletor.hpp"
#include "comum/log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define CABECALHO_CARGAS \
    "data_hora_entrega;tarefa_id;baia;destino;robos;data_hora_coleta;situacao\n"

/* ------------------------------------------------------------------ */
/* Auxiliares                                                          */
/* ------------------------------------------------------------------ */

static void formatar_hora(const struct timespec& ts, char* buf, size_t tam) {

    if(ts.tv_sec == 0 && ts.tv_nsec == 0) {
        snprintf(buf, tam, "-");
        return;
    }

    struct tm t;
    localtime_r(&ts.tv_sec, &t);

    size_t n = strftime(buf, tam, "%Y-%m-%d %H:%M:%S", &t);
    snprintf(buf + n, tam - n, ".%03ld", ts.tv_nsec / 1000000);
}

static long diff_ns(const struct timespec& a, const struct timespec& b) {
    return (b.tv_sec - a.tv_sec) * 1000000000L + (b.tv_nsec - a.tv_nsec);
}

// mkdir -p
static bool criar_diretorio(const std::string& caminho) {

    for(size_t i=1;i<=caminho.size();i++) {

        if(i != caminho.size() && caminho[i] != '/')
            continue;

        std::string parte = caminho.substr(0, i);

        if(mkdir(parte.c_str(), 0775) < 0 && errno != EEXIST) {
            log_msg("COLETOR", "mkdir(%s): %s", parte.c_str(), strerror(errno));
            return false;
        }
    }

    return true;
}

// Abre em modo append; escreve o cabeçalho se o arquivo é novo (ou vazio).
static int abrir_arquivo(const std::string& caminho, const char* cabecalho) {

    int fd = open(caminho.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0664);

    if(fd < 0) {
        log_msg("COLETOR", "open(%s): %s", caminho.c_str(), strerror(errno));
        return -1;
    }

    struct stat st;

    if(cabecalho && fstat(fd, &st) == 0 && st.st_size == 0) {
        if(write(fd, cabecalho, strlen(cabecalho)) < 0)
            log_msg("COLETOR", "write(%s): %s", caminho.c_str(), strerror(errno));
    }

    return fd;
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

ColetorDados::ColetorDados(const std::string& dir)
    : _dir(dir), _shm(NULL), _mq((mqd_t)-1), _fd_cargas(-1),
      _fim_coleta(false), _encerrando(false), _ativo(false),
      _eventos_coletados(0), _descartados_fila(0), _max_fila(0),
      _eventos_gravados(0), _cargas_gravadas(0), _mq_perdidos(0),
      _erros_gravacao(0), _max_latencia_ns(0) {

    pthread_mutex_init(&_mtx, NULL);
    pthread_cond_init(&_cv_fila, NULL);
}

ColetorDados::~ColetorDados() {

    encerrar();

    pthread_cond_destroy(&_cv_fila);
    pthread_mutex_destroy(&_mtx);
}

bool ColetorDados::iniciar(void) {

    if(!criar_diretorio(_dir))
        return false;

    _shm = shm_frota_abrir();

    if(!_shm)
        return false;

    _fd_cargas = abrir_arquivo(_dir + "/cargas.csv", CABECALHO_CARGAS);

    if(_fd_cargas < 0) {
        shm_frota_fechar(_shm);
        _shm = NULL;
        return false;
    }

    // A cópia em tempo real para o Organizador é opcional: sem ela, o disco continua valendo
    _mq = mq_coletor_abrir_escrita();

    if(_mq == (mqd_t)-1)
        log_msg("COLETOR", "sem fila para o Organizador: só grava em disco");

    // A gravação sobe antes: quando a coleta começar, já há quem consuma a fila interna
    if(pthread_create(&_th_gravacao, NULL, ColetorDados::thread_gravacao, this) != 0) {
        log_msg("COLETOR", "falha ao criar thread de gravação");
        return false;
    }

    if(pthread_create(&_th_coleta, NULL, ColetorDados::thread_coleta, this) != 0) {
        log_msg("COLETOR", "falha ao criar thread de coleta");
        pthread_mutex_lock(&_mtx);
        _fim_coleta = true;
        pthread_cond_broadcast(&_cv_fila);
        pthread_mutex_unlock(&_mtx);
        pthread_join(_th_gravacao, NULL);
        return false;
    }

    _ativo = true;

    log_msg("COLETOR", "iniciado: dados em %s/ (histórico por robô + cargas.csv)", _dir.c_str());

    return true;
}

void ColetorDados::encerrar(void) {

    if(!_ativo)
        return;

    _ativo = false;

    // 1. A coleta termina (e esvazia o que ainda está no buffer compartilhado)
    __atomic_store_n(&_encerrando, true, __ATOMIC_RELEASE);
    pthread_join(_th_coleta, NULL);

    // 2. A gravação termina quando a fila interna esvaziar
    pthread_mutex_lock(&_mtx);
    _fim_coleta = true;
    pthread_cond_broadcast(&_cv_fila);
    pthread_mutex_unlock(&_mtx);
    pthread_join(_th_gravacao, NULL);

    // 3. Cargas que foram pegas e nunca entregues (ex.: robô morreu no caminho)
    for(const RegistroCarga& c : _rastreio.pendentes()) {
        gravar_carga(c);
        publicar_carga(c);
    }

    fdatasync(_fd_cargas);
    close(_fd_cargas);
    _fd_cargas = -1;

    for(auto& par : _fd_historico) {
        fdatasync(par.second);
        close(par.second);
    }
    _fd_historico.clear();

    estatisticas();

    if(_mq != (mqd_t)-1) {
        mq_close(_mq);
        _mq = (mqd_t)-1;
    }

    shm_frota_fechar(_shm);
    _shm = NULL;
}

void ColetorDados::estatisticas(void) {

    log_msg("COLETOR",
            "encerrado: %lu eventos gravados, %lu cargas registradas | perdas: %u no buffer "
            "compartilhado, %lu na fila interna, %lu na fila do Organizador, %lu erros de gravação | "
            "fila interna máx. %zu, latência máx. até gravar %ld us",
            _eventos_gravados, _cargas_gravadas,
            _shm ? _shm->eventos.descartados : 0u, _descartados_fila, _mq_perdidos,
            _erros_gravacao, _max_fila, _max_latencia_ns / 1000);
}

/* ------------------------------------------------------------------ */
/* Thread de coleta: buffer compartilhado -> fila interna              */
/* ------------------------------------------------------------------ */

void* ColetorDados::thread_coleta(void* arg) {

    ((ColetorDados*)arg)->laco_coleta();

    return NULL;
}

void ColetorDados::enfileirar(const EventoRobo& ev) {

    pthread_mutex_lock(&_mtx);

    // Nunca bloqueia esperando a gravação: com a fila cheia, descarta e conta
    if(_fila.size() >= LIMITE_FILA_INTERNA) {
        _descartados_fila++;
    } else {
        _fila.push_back(ev);
        _max_fila = std::max(_max_fila, _fila.size());
    }

    pthread_cond_signal(&_cv_fila);
    pthread_mutex_unlock(&_mtx);

    _eventos_coletados++;
}

void ColetorDados::laco_coleta(void) {

    EventoRobo ev;

    // Bloqueia no semáforo "cheios" do buffer; o timeout só serve para checar o encerramento
    while(!__atomic_load_n(&_encerrando, __ATOMIC_ACQUIRE)) {

        if(shm_evento_consumir(_shm, ev, 100) == 0)
            enfileirar(ev);
    }

    // Esvazia o que ficou no buffer compartilhado
    while(shm_evento_consumir(_shm, ev, 0) == 0)
        enfileirar(ev);
}

/* ------------------------------------------------------------------ */
/* Thread de gravação: fila interna -> disco / Organizador             */
/* ------------------------------------------------------------------ */

void* ColetorDados::thread_gravacao(void* arg) {

    ((ColetorDados*)arg)->laco_gravacao();

    return NULL;
}

void ColetorDados::laco_gravacao(void) {

    std::vector<EventoRobo> lote;

    pthread_mutex_lock(&_mtx);

    while(true) {

        while(_fila.empty() && !_fim_coleta)
            pthread_cond_wait(&_cv_fila, &_mtx);

        if(_fila.empty())       // _fim_coleta e nada mais a gravar
            break;

        // Pega a fila inteira de uma vez e solta o mutex: a coleta não espera o disco
        lote.clear();
        lote.swap(_fila);

        pthread_mutex_unlock(&_mtx);
        processar_lote(lote);
        pthread_mutex_lock(&_mtx);
    }

    pthread_mutex_unlock(&_mtx);
}

void ColetorDados::processar_lote(std::vector<EventoRobo>& lote) {

    _sujos.clear();

    for(const EventoRobo& ev : lote)
        processar(ev);

    // Um fdatasync por arquivo tocado no lote (e não por evento)
    for(int fd : _sujos)
        fdatasync(fd);
}

void ColetorDados::processar(const EventoRobo& ev) {

    gravar_historico(ev);

    RegistroCarga carga;

    if(_rastreio.processar(ev, carga)) {
        gravar_carga(carga);
        publicar_carga(carga);
    }

    publicar_evento(ev);

    struct timespec agora;
    clock_gettime(CLOCK_REALTIME, &agora);

    _max_latencia_ns = std::max(_max_latencia_ns, diff_ns(ev.timestamp, agora));
    _eventos_gravados++;
}

/* ------------------------------------------------------------------ */
/* Gravação em disco                                                   */
/* ------------------------------------------------------------------ */

bool ColetorDados::gravar(int fd, const char* texto, size_t n) {

    while(n > 0) {

        ssize_t r = write(fd, texto, n);

        if(r < 0) {

            if(errno == EINTR)
                continue;

            // Loga só o primeiro erro para não inundar o log
            if(_erros_gravacao++ == 0)
                log_msg("COLETOR", "erro de gravação: %s", strerror(errno));

            return false;
        }

        texto += r;
        n -= r;
    }

    return true;
}

int ColetorDados::fd_historico(int robo_id) {

    auto it = _fd_historico.find(robo_id);

    if(it != _fd_historico.end())
        return it->second;

    char nome[64];
    snprintf(nome, sizeof(nome), "/historico_robo_%d.log", robo_id);

    int fd = abrir_arquivo(_dir + nome, NULL);

    if(fd >= 0)
        _fd_historico[robo_id] = fd;
    else
        _erros_gravacao++;

    return fd;
}

// Uma linha por evento: "[data hora.ms] TIPO: texto"
void ColetorDados::gravar_historico(const EventoRobo& ev) {

    int fd = fd_historico(ev.robo_id);

    if(fd < 0)
        return;

    char hora[40];
    formatar_hora(ev.timestamp, hora, sizeof(hora));

    char linha[TAM_TEXTO_EVENTO + 96];
    int n = snprintf(linha, sizeof(linha), "[%s] %s: %s\n", hora, tipo_evento_robo_nome(ev.tipo), ev.texto);

    if(gravar(fd, linha, n))
        _sujos.insert(fd);
}

// Rastreabilidade: uma linha por carga, com ';' como separador
void ColetorDados::gravar_carga(const RegistroCarga& c) {

    char entrega[40], coleta[40];
    formatar_hora(c.t_entrega, entrega, sizeof(entrega));
    formatar_hora(c.t_coleta, coleta, sizeof(coleta));

    std::string robos;

    for(int id : c.robos)
        robos += (robos.empty() ? "" : "+") + std::to_string(id);

    const char* situacao = !c.completa ? "incompleta"
                         : c.coleta_registrada ? "entregue" : "entregue_sem_registro_de_coleta";

    char linha[256];
    int n = snprintf(linha, sizeof(linha), "%s;%d;%s;%s;%s;%s;%s\n", entrega, c.tarefa_id,
                     c.baia.c_str(), c.destino.empty() ? "-" : c.destino.c_str(),
                     robos.c_str(), coleta, situacao);

    if(gravar(_fd_cargas, linha, n)) {
        fdatasync(_fd_cargas);      // rastreabilidade é o dado mais importante: vai direto ao disco
        _cargas_gravadas++;
    }
}

/* ------------------------------------------------------------------ */
/* Disponibilização ao Organizador (operação remota)                   */
/* ------------------------------------------------------------------ */

void ColetorDados::publicar_evento(const EventoRobo& ev) {

    if(_mq == (mqd_t)-1)
        return;

    RegistroColetor r;
    memset(&r, 0, sizeof(r));

    r.tipo = REG_EVENTO;
    r.timestamp = ev.timestamp;
    r.robo_id = ev.robo_id;
    r.tipo_evento = ev.tipo;
    snprintf(r.texto, sizeof(r.texto), "%s", ev.texto);

    if(mq_coletor_enviar(_mq, r) < 0)
        _mq_perdidos++;
}

void ColetorDados::publicar_carga(const RegistroCarga& c) {

    if(_mq == (mqd_t)-1)
        return;

    RegistroColetor r;
    memset(&r, 0, sizeof(r));

    r.tipo = REG_CARGA;
    r.timestamp = c.t_entrega;
    r.t_coleta = c.t_coleta;
    r.tarefa_id = c.tarefa_id;
    snprintf(r.baia, sizeof(r.baia), "%s", c.baia.c_str());
    snprintf(r.destino, sizeof(r.destino), "%s", c.destino.c_str());
    r.completa = c.completa;
    r.n_robos = (int)std::min(c.robos.size(), (size_t)MAX_ROBOS_CARGA);

    for(int i=0;i<r.n_robos;i++)
        r.robos[i] = c.robos[i];

    if(mq_coletor_enviar(_mq, r) < 0)
        _mq_perdidos++;
}
